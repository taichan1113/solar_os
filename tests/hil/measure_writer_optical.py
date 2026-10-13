"""Relative camera contrast for the SAME exported T0 black mask.

Requires Pillow only for file loading/rectification. Never modifies input photos.
Corners must be active screen TL, TR, BR, BL in each photograph. The output is
camera intensity, not calibrated reflectance; sensitivity/noise are proxies,
not a statistical confidence interval or panel-lifetime measurement.
"""
import argparse
import json
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


def main():
    from PIL import Image, ImageOps, ExifTags
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--analysis',type=Path,required=True)
    p.add_argument('--t0',type=Path,required=True)
    p.add_argument('--t1',type=Path,required=True)
    p.add_argument('--corners',type=Path,required=True,help='JSON with T0/T1 screen corners and camera_conditions')
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();analysis=json.loads(a.analysis.read_text(encoding='utf-8'))
    if not analysis.get('valid') or analysis.get('protocol')!='continuous-optical':
        raise ValueError('requires a complete, validated optical journal')
    config=json.loads(a.corners.read_text(encoding='utf-8')); samplers=[];metadata=[]
    for name,path in (('T0',a.t0),('T1',a.t1)):
        image=Image.open(path)
        exif={ExifTags.TAGS.get(k,str(k)):str(v) for k,v in image.getexif().items()
              if ExifTags.TAGS.get(k) in ('DateTime','ExposureTime','FNumber','ISOSpeedRatings','Orientation')}
        source_size=image.size
        image=ImageOps.exif_transpose(image)
        metadata.append(dict(point=name,source=str(path),source_size=source_size,size=image.size,exif=exif))
        # Corners apply to the visible image after EXIF orientation correction.
        coeff=homography(config[name],3200,1920)
        rectified=image.convert('L').transform((3200,1920),Image.Transform.PERSPECTIVE,
            coeff,Image.Resampling.BICUBIC)
        samplers.append(lambda x,y,img=rectified:img.getpixel((int(x*4),int(y*4))))
    result=dict(camera_intensity_only=True,metadata=metadata,
        camera_conditions=config.get('camera_conditions','unrecorded'),
        photos_elapsed_after_completion_s=config.get('elapsed_after_completion_s',{'T0':None,'T1':None}),
        source_points=analysis['rows']['point'],regions={})
    for name,mask in analysis['black_masks'].items():
        result['regions'][name]=compare_roi(*samplers,mask)
    a.output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({name:{k:m[k] for k in ('R','contrast_drop','white_relative_change',
        'fading_exceeds_shift_and_white_noise_proxy')} for name,m in result['regions'].items()},indent=2))


if __name__=='__main__':main()
