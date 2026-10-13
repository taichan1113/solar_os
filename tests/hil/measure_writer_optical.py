"""Relative camera contrast for the SAME exported T0 black mask.

Requires Pillow only for file loading/rectification. Never modifies input photos.
Corners must be active screen TL, TR, BR, BL in each photograph. The output is
camera intensity, not calibrated reflectance; sensitivity/noise are proxies,
not a statistical confidence interval or panel-lifetime measurement.
"""
import argparse
from datetime import datetime, timedelta, timezone
import hashlib
import json
import math
import statistics
from pathlib import Path


def homography(corners, width=800, height=480):
    rows=[]
    for (x,y),(u,v) in zip(((0,0),(width,0),(width,height),(0,height)),corners):
        rows.append([x,y,1,0,0,0,-u*x,-u*y,u])
        rows.append([0,0,0,x,y,1,-v*x,-v*y,v])
    for col in range(8):
        pivot=max(range(col,8),key=lambda i:abs(rows[i][col]))
        rows[col],rows[pivot]=rows[pivot],rows[col]
        if abs(rows[col][col])<1e-10: raise ValueError('degenerate screen corners')
        a=rows[col][col];rows[col]=[v/a for v in rows[col]]
        for i in range(8):
            if i!=col:
                a=rows[i][col];rows[i]=[v-a*w for v,w in zip(rows[i],rows[col])]
    return [rows[i][-1] for i in range(8)]


def black_coordinates(mask):
    x,y,w,h=mask['roi']
    if not (0<=x<x+w<=800 and 0<=y<y+h<=480) or len(mask['rows'])!=h:
        raise ValueError('invalid mask ROI/rows')
    result=set()
    for j,line in enumerate(mask['rows']):
        row=bytes.fromhex(line)
        if len(row)!=(w+7)//8: raise ValueError('mask row length')
        for i in range(w):
            if row[i//8] & (1<<(7-i%8)):result.add((x+i,y+j))
    if len(result)!=mask['black_pixels'] or not result: raise ValueError('mask pixel count')
    return result


def stats(values):
    median=statistics.median(values)
    return median,statistics.median(abs(x-median) for x in values)


def register_mask(sample,mask,bound=4):
    """Bounded local binary-template registration; never change the T0 mask.

    Nearby white strokes/holes help distinguish the glyph from its border.
    Score is normalized to avoid choosing a pose based on absolute brightness.
    Boundary hits are unreliable and must not be reported as valid alignment.
    """
    if not 0<bound<=4:raise ValueError('registration bound must be in (0,4]')
    black=sorted(black_coordinates(mask));x,y,w,h=mask['roi']
    near={(a+dx,b+dy) for a,b in black for dx in (-1,0,1) for dy in (-1,0,1)}
    white=sorted((near-set(black))&{(a,b) for a in range(x,x+w) for b in range(y,y+h)})
    if not white:raise ValueError('registration requires neighboring white pixels')
    def score(dx,dy):
        B=[sample(a+.5+dx,b+.5+dy) for a,b in black]
        W=[sample(a+.5+dx,b+.5+dy) for a,b in white]
        n=len(B)+len(W);mean=(sum(B)+sum(W))/n
        variance=sum((v-mean)**2 for v in B+W)/n
        return (sum(W)/len(W)-sum(B)/len(B))/math.sqrt(variance) if variance else -math.inf
    coarse=[(score(i/2,j/2),i/2,j/2) for i in range(-int(bound*2),int(bound*2)+1)
            for j in range(-int(bound*2),int(bound*2)+1)]
    _,cx,cy=max(coarse)
    fine=[(score(cx+i/8,cy+j/8),cx+i/8,cy+j/8) for i in range(-4,5) for j in range(-4,5)
          if abs(cx+i/8)<=bound and abs(cy+j/8)<=bound]
    value,dx,dy=max(fine)
    return dict(dx=dx,dy=dy,normalized_template_score=value,
                boundary_hit=abs(dx)==bound or abs(dy)==bound,search_bound=bound)


def compare_roi(sample0,sample1,mask):
    black=black_coordinates(mask)
    x,y,w,h=mask['roi']
    adjacent={(a+dx,b+dy) for a,b in black for dx in (-1,0,1) for dy in (-1,0,1)}
    white={(a,b) for a in range(x,x+w) for b in range(y,y+h) if (a,b) not in adjacent}
    if len(white)<10:raise ValueError('not enough uncontaminated white pixels')
    results=[]
    for sampler in (sample0,sample1):
        contrast=[]
        for dx in (-.25,0,.25):
            for dy in (-.25,0,.25):
                W,Wmad=stats([sampler(a+.5+dx,b+.5+dy) for a,b in white])
                B,Bmad=stats([sampler(a+.5+dx,b+.5+dy) for a,b in black])
                if W<=0:raise ValueError('zero white intensity')
                contrast.append((W-B)/W)
                if dx==dy==0:central=dict(W=W,B=B,C=(W-B)/W,white_MAD=Wmad,black_MAD=Bmad)
        central.update(C_shift_min=min(contrast),C_shift_max=max(contrast))
        results.append(central)
    a,b=results
    if a['C']<=0:raise ValueError('initial contrast is not positive')
    noise=2*1.4826*(a['white_MAD']/a['W']+b['white_MAD']/b['W'])
    gap=a['C_shift_min']-b['C_shift_max']
    return dict(roi=mask['roi'],black_pixels=len(black),white_pixels=len(white),
        T0=a,T1=b,R=b['C']/a['C'],contrast_drop=a['C']-b['C'],
        white_relative_change=b['W']/a['W']-1,
        white_noise_proxy=noise,quarter_pixel_shift_gap=gap,
        fading_exceeds_shift_and_white_noise_proxy=gap>noise,
        caveat='Sensitivity/noise proxies do not exclude exposure, focus or lighting changes.')


def compare_with_reference(samplers,masks,reference,bound=4):
    """Fit only an independent OTP reference, then transfer its translation.

    Screen corners handle perspective first. This residual translation is not
    a lens-distortion model. Measured target pixels never enter the pose fit.
    The reference's own contrast is ancillary, since it participates in fitting.
    """
    if len(samplers)!=2:raise ValueError('T0 and T1 photographs required')
    if reference in ('first','group'):raise ValueError('measurement target cannot be the registration reference')
    if reference not in masks:raise ValueError('registration reference mask missing')
    anchor=masks[reference];x,y,w,h=anchor['roi']
    # Even the search envelope must not sample any target ROI. Reject overlap
    # instead of silently deriving alignment from the measurement target.
    for name,mask in masks.items():
        if name==reference:continue
        a,b,c,d=mask['roi']
        if x-bound<a+c and a<x+w+bound and y-bound<b+d and b<y+h+bound:
            raise ValueError('reference search envelope overlaps target ROI')
    offsets=[register_mask(sampler,anchor,bound) for sampler in samplers]
    if any(o['boundary_hit'] or not math.isfinite(o['normalized_template_score']) for o in offsets):
        raise ValueError('reference registration failed or hit search boundary')
    selected=[lambda x,y,s=s,o=o:s(x+o['dx'],y+o['dy']) for s,o in zip(samplers,offsets)]
    measured={name:compare_roi(*selected,mask) for name,mask in masks.items()}
    for name,region in measured.items():
        region['registration_reference']=reference
        region['used_for_registration']=name==reference
        a,b,c,d=masks[name]['roi']
        region['distance_from_reference_px']=math.hypot(a+c/2-x-w/2,b+d/2-y-h/2)
    return measured,dict(zip(('T0','T1'),offsets))


def photograph_timing(metadata,config):
    """Camera clock minus PC clock is subtracted, never guessed from locale.

    Completion UTC must be correlated to the device journal by the operator.
    Exif second/subsecond precision is recorded separately from clock uncertainty.
    """
    result={}
    for meta in metadata:
        name=meta['point'];exif=meta.get('exif',{})
        record=dict(capture_utc=None,completion_utc=None,elapsed_after_completion_s=None,
                    timestamp_resolution_s=None,error=None)
        try:
            stamp=datetime.strptime(exif['DateTimeOriginal'],'%Y:%m:%d %H:%M:%S')
            offset=exif.get('OffsetTimeOriginal',config.get('camera_utc_offset'))
            if not isinstance(offset,str) or len(offset)!=6 or offset[0] not in '+-' or offset[3]!=':':
                raise ValueError('camera timezone missing or invalid')
            hours=int(offset[1:3]);minutes=int(offset[4:6])
            if hours>23 or minutes>59:raise ValueError('camera timezone out of range')
            delta=timedelta(hours=hours,minutes=minutes)*(1 if offset[0]=='+' else -1)
            subsec=exif.get('SubsecTimeOriginal','')
            if subsec and not subsec.isdigit():raise ValueError('Exif subsecond invalid')
            stamp=stamp.replace(tzinfo=timezone(delta))+timedelta(seconds=float('0.'+subsec) if subsec else 0)
            record['timestamp_resolution_s']=10**(-len(subsec)) if subsec else 1
            camera_offset=config.get('clock_offset_s')
            if not isinstance(camera_offset,(float,int)) or isinstance(camera_offset,bool) or not math.isfinite(camera_offset):
                raise ValueError('camera-to-PC clock offset unknown')
            stamp=stamp.astimezone(timezone.utc)-timedelta(seconds=camera_offset)
            completion=datetime.fromisoformat(config['completion_utc'][name].replace('Z','+00:00'))
            if completion.tzinfo is None:raise ValueError('completion UTC timezone missing')
            record.update(capture_utc=stamp.isoformat(),completion_utc=completion.isoformat(),
                          elapsed_after_completion_s=(stamp-completion).total_seconds())
        except (KeyError,ValueError,TypeError,OverflowError) as exc:
            record['error']=str(exc)
        result[name]=record
    return result


def stage_a_prerequisites(config,metadata,masks,reference):
    """Fail closed on missing evidence; declarations are not automatic acceptance.

    No arbitrary exposure/shadow threshold substitutes for operator review.
    Original dimensions/provenance are declarations compared with decoded input,
    not proof that an upscaled file really came from the camera.
    """
    reasons=[]
    checks=config.get('stage_a_checks',{})
    required=('panel_fixed','camera_fixed','shadow_free','uniform_light','ae_af_locked',
              'no_digital_zoom','screen_and_reference_visible','clock_difference_checked',
              'processing_uncertainty_acceptable','timing_uncertainty_acceptable',
              'independent_alignment_reviewed','white_variation_acceptable')
    for name in required:
        if checks.get(name) is not True:reasons.append('unconfirmed: '+name)
    originals=config.get('image_provenance',{})
    by_point={m['point']:m for m in metadata}
    for name in ('T0','T1'):
        info=originals.get(name,{})
        meta=by_point.get(name)
        if meta is None:
            reasons.append(name+': photograph missing');continue
        if info.get('original_resolution') is not True:
            reasons.append(name+': original resolution unconfirmed')
        dimensions=info.get('original_dimensions')
        if not isinstance(dimensions,(list,tuple)) or list(dimensions)!=list(meta['source_size']):
            reasons.append(name+': original and decoded dimensions differ or are missing')
        history=info.get('conversion_history')
        if not isinstance(history,list) or not all(isinstance(step,str) for step in history):
            reasons.append(name+': conversion history missing')
    timing=photograph_timing(metadata,config)
    delays=[timing.get(name,{}).get('elapsed_after_completion_s') for name in ('T0','T1')]
    def finite(value):
        return isinstance(value,(float,int)) and not isinstance(value,bool) and math.isfinite(value)
    tolerance=config.get('timing_tolerance_s')
    if not all(finite(v) and v>=0 for v in delays):
        reasons.append('Exif/clock-correlated completion-to-shot delays missing or invalid')
    elif not finite(tolerance) or tolerance<=0 or abs(delays[0]-delays[1])>tolerance:
        reasons.append('shot-delay agreement not established')
    for name in ('clock_offset_s','clock_uncertainty_s'):
        if not finite(config.get(name)) or (name=='clock_uncertainty_s' and config[name]<0):
            reasons.append(name+': missing or invalid')
    if reference not in masks or reference in ('first','group') or config.get('registration_reference_role')!='OTP':
        reasons.append('independent OTP registration reference unconfirmed')
    if 'first' not in masks or 'group' not in masks:
        reasons.append('central first-a and simultaneously formed repeated-glyph masks missing')
    else:
        for name in ('first','group'):
            x,y,w,h=masks[name]['roi']
            if not (160<=x+w/2<=640 and 96<=y+h/2<=384):
                reasons.append(name+': target is outside central panel region')
        if masks['group']['black_pixels']<=masks['first']['black_pixels']:
            reasons.append('group mask does not contain more initial black pixels')
    return dict(ready_for_review=not reasons,reasons=reasons,
        stage_b_authorized=False,
        caveat='Check declarations, source provenance, alignment and white statistics manually; no automatic GO.')


def main():
    from PIL import Image, ImageOps, ExifTags
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--analysis',type=Path,required=True)
    p.add_argument('--t0',type=Path,required=True)
    p.add_argument('--t1',type=Path,required=True)
    p.add_argument('--corners',type=Path,required=True,help='JSON with T0/T1 screen corners and camera_conditions')
    p.add_argument('--register-local',action='store_true',help='Bounded local same-mask registration after corner alignment; report offsets')
    p.add_argument('--registration-reference',help='Fit only this independent OTP mask and transfer the same offset to all target ROIs')
    p.add_argument('--stage-a',action='store_true',help='Require original files, independent reference and recorded shooting conditions; emit no contrast values if evidence is incomplete')
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.register_local and (a.registration_reference or a.stage_a):
        p.error('--register-local cannot be used with independent reference or Stage A')
    analysis=json.loads(a.analysis.read_text(encoding='utf-8'))
    if not analysis.get('valid') or analysis.get('protocol')!='continuous-optical':
        raise ValueError('requires a complete, validated optical journal')
    config=json.loads(a.corners.read_text(encoding='utf-8')); samplers=[];metadata=[]
    for name,path in (('T0',a.t0),('T1',a.t1)):
        if path.suffix.lower() in ('.heic','.heif'):
            try:
                import pillow_heif
            except ImportError as exc:
                raise ValueError('HEIC decoder unavailable; use an original-resolution conversion and record its history') from exc
            pillow_heif.register_heif_opener()
        image=Image.open(path)
        exif={ExifTags.TAGS.get(k,str(k)):str(v) for k,v in image.getexif().items()
              if ExifTags.TAGS.get(k) in ('DateTime','ExposureTime','FNumber','ISOSpeedRatings','Orientation','Make','Model','Software')}
        # iPhone capture settings/time normally live in the Exif sub-IFD.
        for k,v in image.getexif().get_ifd(34665).items():
            key=ExifTags.TAGS.get(k,str(k))
            if key in ('DateTimeOriginal','SubsecTimeOriginal','OffsetTimeOriginal','ExposureTime',
                       'FNumber','ISOSpeedRatings','ExposureMode','WhiteBalance','ColorSpace',
                       'PixelXDimension','PixelYDimension','LensModel','FocalLength'):
                exif[key]=str(v)
        source_size=image.size;source_format=image.format
        image=ImageOps.exif_transpose(image)
        metadata.append(dict(point=name,source=str(path),source_size=source_size,size=image.size,exif=exif,
            sha256=hashlib.sha256(path.read_bytes()).hexdigest(),format=source_format,
            conversion_history=config.get('image_provenance',{}).get(name,{}).get('conversion_history'),
            analysis_processing=['EXIF orientation','Pillow grayscale','3200x1920 bicubic homography',
                                 'bilinear intensity sampling at logical pixel centers'],
            upsampling_recovers_source_resolution=False))
        # Corners apply to the visible image after EXIF orientation correction.
        coeff=homography(config[name],3200,1920)
        rectified=image.convert('L').transform((3200,1920),Image.Transform.PERSPECTIVE,
            coeff,Image.Resampling.BICUBIC)
        def sampler(x,y,img=rectified):
            x*=4;y*=4;ix=math.floor(x);iy=math.floor(y);dx=x-ix;dy=y-iy
            if not (0<=ix<3199 and 0<=iy<1919):raise ValueError('sample outside rectified panel')
            return ((1-dx)*(1-dy)*img.getpixel((ix,iy))+dx*(1-dy)*img.getpixel((ix+1,iy))+
                    (1-dx)*dy*img.getpixel((ix,iy+1))+dx*dy*img.getpixel((ix+1,iy+1)))
        samplers.append(sampler)
    result=dict(camera_intensity_only=True,metadata=metadata,
        camera_conditions=config.get('camera_conditions','unrecorded'),
        photos_elapsed_after_completion_s=config.get('elapsed_after_completion_s',{'T0':None,'T1':None}),
        source_points=analysis['rows']['point'],regions={},
        registration_method='corner homography; optional bounded local normalized T0-mask correlation')
    result['photograph_timing']=photograph_timing(metadata,config)
    if a.stage_a:
        result['photos_elapsed_after_completion_s']={name:point['elapsed_after_completion_s']
            for name,point in result['photograph_timing'].items()}
        result['stage_a']=stage_a_prerequisites(config,metadata,analysis['black_masks'],a.registration_reference)
        if not result['stage_a']['ready_for_review']:
            a.output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
            print(json.dumps(result['stage_a'],ensure_ascii=False,indent=2))
            return 2
    if a.registration_reference:
        result['regions'],result['reference_registration']=compare_with_reference(
            samplers,analysis['black_masks'],a.registration_reference)
        result['registration_method']='corner homography; independent OTP reference translation transferred to all targets'
        result['registration_caveat']='Reference contrast participates in pose fitting; translation does not correct local lens distortion.'
    for name,mask in analysis['black_masks'].items():
        if a.registration_reference:break
        selected=samplers
        if a.register_local:
            offsets=[register_mask(sampler,mask) for sampler in samplers]
            if any(o['boundary_hit'] for o in offsets):raise ValueError('local registration hit search boundary; contrast is unreliable')
            selected=[lambda x,y,s=s,o=o:s(x+o['dx'],y+o['dy']) for s,o in zip(samplers,offsets)]
        measured=compare_roi(*selected,mask)
        if a.register_local:measured['local_registration']=dict(zip(('T0','T1'),offsets))
        result['regions'][name]=measured
    a.output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({name:{k:m[k] for k in ('R','contrast_drop','white_relative_change',
        'fading_exceeds_shift_and_white_noise_proxy')} for name,m in result['regions'].items()},indent=2))


if __name__=='__main__':raise SystemExit(main())
