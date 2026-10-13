import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('measure',Path(__file__).parent/'hil/measure_writer_optical.py')
measure=importlib.util.module_from_spec(spec);spec.loader.exec_module(measure)


class OpticalTests(unittest.TestCase):
    mask=dict(roi=[0,0,16,10],rows=['0f00']*10,black_pixels=40)

    def sampler(self,black,white):
        return lambda x,y:black if 4<=int(x)<8 and 0<=int(y)<10 else white

    def test_same_mask_detects_fading_and_normalizes_exposure(self):
        original=self.sampler(40,200)
        faded=self.sampler(80,200)
        same_exposure_changed=self.sampler(50,250)
        self.assertAlmostEqual(measure.compare_roi(original,faded,self.mask)['R'],.75)
        self.assertTrue(measure.compare_roi(original,faded,self.mask)['fading_exceeds_shift_and_white_noise_proxy'])
        self.assertAlmostEqual(measure.compare_roi(original,same_exposure_changed,self.mask)['R'],1)

    def test_corner_mapping_and_degeneracy(self):
        coeff=measure.homography([[10,20],[810,20],[810,500],[10,500]])
        self.assertEqual([round(v,6) for v in coeff],[1,0,10,0,1,20,0,0])
        with self.assertRaises(ValueError):measure.homography([[0,0]]*4)

    def test_empty_wrong_masks_and_white_area_rejected(self):
        for bad in (dict(self.mask,black_pixels=39),dict(self.mask,rows=['0000']*10),
                    dict(self.mask,roi=[799,0,16,10])):
            with self.assertRaises(ValueError):measure.compare_roi(self.sampler(40,200),self.sampler(80,200),bad)

    def test_local_registration_tracks_same_glyph_under_fading(self):
        # A shifted smooth rectangular glyph, with ample white on all sides.
        mask=dict(roi=[10,10,16,14],rows=['0000']*4+['0f00']*6+['0000']*4,black_pixels=24)
        import math
        def image(level):
            def sample(x,y):
                # Half-pixel edges, smooth enough to avoid a plateau in pose fit.
                x-=.75;y+=.5
                sigmoid=lambda t:1/(1+math.exp(-8*t))
                ink=sigmoid(x-14)*sigmoid(18-x)*sigmoid(y-14)*sigmoid(20-y)
                return 200-(200-level)*ink
            return sample
        initial=measure.register_mask(image(40),mask)
        faded=measure.register_mask(image(80),mask)
        self.assertEqual((initial['dx'],initial['dy']),(.75,-.5))
        self.assertEqual((initial['dx'],initial['dy']),(faded['dx'],faded['dy']))
        self.assertAlmostEqual(initial['normalized_template_score'],faded['normalized_template_score'])
        self.assertFalse(initial['boundary_hit'])
        s0=lambda x,y:image(40)(x+initial['dx'],y+initial['dy'])
        s1=lambda x,y:image(80)(x+faded['dx'],y+faded['dy'])
        self.assertAlmostEqual(measure.compare_roi(s0,s1,mask)['R'],.75,places=3)

    def test_registration_boundary_and_missing_white(self):
        mask=dict(roi=[10,10,16,14],rows=['0000']*4+['0f00']*6+['0000']*4,black_pixels=24)
        shifted=lambda x,y:40 if 17<=x<21 and 14<=y<20 else 200
        self.assertTrue(measure.register_mask(shifted,mask,bound=1)['boundary_hit'])
        with self.assertRaises(ValueError):measure.register_mask(shifted,mask,bound=5)

    def test_reference_pose_does_not_follow_target_darkness_or_displacement(self):
        import math
        def mask(x):
            return dict(roi=[x,10,16,14],rows=['0000']*4+['0f00']*6+['0000']*4,black_pixels=24)
        masks={'fixed':mask(10),'first':mask(50)}
        def image(level,target_dx=0):
            def sample(x,y):
                sigmoid=lambda t:1/(1+math.exp(-max(-500,min(500,8*t))))
                def ink(left,shift):
                    u=x-.75-shift;v=y+.5
                    return sigmoid(u-left)*sigmoid(left+4-u)*sigmoid(v-14)*sigmoid(20-v)
                return 200-160*ink(14,0)-(200-level)*ink(54,target_dx)
            return sample
        unchanged,pose0=measure.compare_with_reference([image(40),image(40)],masks,'fixed')
        faded,pose1=measure.compare_with_reference([image(40),image(80)],masks,'fixed')
        shifted,pose2=measure.compare_with_reference([image(40),image(80,2.5)],masks,'fixed')
        self.assertEqual(pose0,pose1)
        self.assertEqual(pose0,pose2)
        self.assertEqual((pose0['T1']['dx'],pose0['T1']['dy']),(.75,-.5))
        self.assertAlmostEqual(unchanged['first']['R'],1)
        self.assertAlmostEqual(faded['first']['R'],.75,places=3)
        self.assertFalse(faded['first']['used_for_registration'])
        self.assertTrue(faded['fixed']['used_for_registration'])
        self.assertNotEqual(shifted['first']['R'],faded['first']['R'])

    def test_reference_overlap_missing_photos_and_failed_alignment_rejected(self):
        mask=dict(roi=[10,10,16,14],rows=['0000']*4+['0f00']*6+['0000']*4,black_pixels=24)
        sampler=lambda x,y:40 if 17<=x<21 and 14<=y<20 else 200
        with self.assertRaisesRegex(ValueError,'overlaps'):
            measure.compare_with_reference([sampler,sampler],{'fixed':mask,'first':mask},'fixed')
        with self.assertRaisesRegex(ValueError,'photographs'):
            measure.compare_with_reference([sampler],{'fixed':mask},'fixed')
        with self.assertRaisesRegex(ValueError,'reference mask missing'):
            measure.compare_with_reference([sampler,sampler],{'first':mask},'fixed')
        with self.assertRaisesRegex(ValueError,'measurement target'):
            measure.compare_with_reference([sampler,sampler],{'first':mask},'first')
        with self.assertRaisesRegex(ValueError,'registration failed'):
            measure.compare_with_reference([sampler,sampler],{'fixed':mask},'fixed',bound=1)
        flat=lambda x,y:200
        with self.assertRaisesRegex(ValueError,'registration failed'):
            measure.compare_with_reference([flat,flat],{'fixed':mask},'fixed')

    def stage_fixture(self):
        checks={name:True for name in ('panel_fixed','camera_fixed','shadow_free','uniform_light',
            'ae_af_locked','no_digital_zoom','screen_and_reference_visible','clock_difference_checked',
            'processing_uncertainty_acceptable','timing_uncertainty_acceptable',
            'independent_alignment_reviewed','white_variation_acceptable')}
        config=dict(stage_a_checks=checks,
            image_provenance={p:dict(original_resolution=True,original_dimensions=[4032,3024],
                conversion_history=[]) for p in ('T0','T1')},
            elapsed_after_completion_s={'T0':5,'T1':5.1},timing_tolerance_s=.5,
            completion_utc={'T0':'2026-10-13T03:00:00Z','T1':'2026-10-13T03:01:00Z'},
            clock_offset_s=0,clock_uncertainty_s=.2,registration_reference_role='OTP')
        metadata=[dict(point=p,source_size=[4032,3024],exif=dict(DateTimeOriginal=stamp,
            OffsetTimeOriginal='+09:00',SubsecTimeOriginal='1'))
            for p,stamp in (('T0','2026:10:13 12:00:05'),('T1','2026:10:13 12:01:05'))]
        masks={'fixed':dict(roi=[180,202,120,22],black_pixels=250),
               'first':dict(roi=[320,202,10,22],black_pixels=47),
               'group':dict(roi=[320,202,120,22],black_pixels=564)}
        return config,metadata,masks

    def test_stage_a_is_review_only_and_missing_conditions_fail_closed(self):
        config,metadata,masks=self.stage_fixture()
        result=measure.stage_a_prerequisites(config,metadata,masks,'fixed')
        self.assertTrue(result['ready_for_review'])
        self.assertFalse(result['stage_b_authorized'])
        for name in list(config['stage_a_checks']):
            old=config['stage_a_checks'].pop(name)
            self.assertFalse(measure.stage_a_prerequisites(config,metadata,masks,'fixed')['ready_for_review'])
            config['stage_a_checks'][name]=old
        self.assertFalse(measure.stage_a_prerequisites(config,metadata,masks,None)['ready_for_review'])

    def test_stage_a_rejects_resized_missing_delayed_or_uncentered_evidence(self):
        import copy
        config,metadata,masks=self.stage_fixture()
        cases=[]
        c=copy.deepcopy(config);c['image_provenance']['T0']['original_resolution']=False;cases.append((c,metadata,masks))
        c=copy.deepcopy(config);c['image_provenance']['T0'].pop('conversion_history');cases.append((c,metadata,masks))
        m=copy.deepcopy(metadata);m[0]['source_size']=[1824,1368];cases.append((config,m,masks))
        cases.append((config,metadata[:1],masks))
        c=copy.deepcopy(config);c.pop('completion_utc');cases.append((c,metadata,masks))
        m=copy.deepcopy(metadata);m[1]['exif']['DateTimeOriginal']='2026:10:13 12:01:40';cases.append((config,m,masks))
        c=copy.deepcopy(config);c['clock_offset_s']=float('nan');cases.append((c,metadata,masks))
        cases.append((config,metadata,{'first':masks['first']}))
        m=copy.deepcopy(masks);m['first']['roi'][0]=4;cases.append((config,metadata,m))
        m=copy.deepcopy(masks);m['group']['black_pixels']=47;cases.append((config,metadata,m))
        for c,meta,msk in cases:
            self.assertFalse(measure.stage_a_prerequisites(c,meta,msk,'fixed')['ready_for_review'])

    def test_exif_shot_delay_requires_explicit_clock_and_timezone(self):
        import copy
        config,metadata,_=self.stage_fixture()
        config['clock_offset_s']=.2
        timing=measure.photograph_timing(metadata,config)
        self.assertAlmostEqual(timing['T0']['elapsed_after_completion_s'],4.9)
        self.assertEqual(timing['T0']['timestamp_resolution_s'],.1)
        metadata[0]['exif'].pop('OffsetTimeOriginal')
        self.assertIsNone(measure.photograph_timing(metadata,config)['T0']['elapsed_after_completion_s'])
        config['camera_utc_offset']='+09:00'
        self.assertAlmostEqual(measure.photograph_timing(metadata,config)['T0']['elapsed_after_completion_s'],4.9)
        for bad in ('+24:00','+09:99','unknown'):
            c=copy.deepcopy(config);c['camera_utc_offset']=bad
            self.assertIsNone(measure.photograph_timing(metadata,c)['T0']['elapsed_after_completion_s'])
        config.pop('clock_offset_s')
        self.assertIsNone(measure.photograph_timing(metadata,config)['T1']['elapsed_after_completion_s'])

    def test_stage_a_cli_emits_no_contrast_when_shooting_evidence_is_missing(self):
        import json
        import subprocess
        import sys
        import tempfile
        try:
            from PIL import Image
        except ImportError:
            self.skipTest('Pillow unavailable')
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            analysis=dict(valid=True,protocol='continuous-optical',rows={'point':[]},black_masks={'first':self.mask})
            (root/'analysis.json').write_text(json.dumps(analysis))
            corners={'T0':[[0,0],[800,0],[800,480],[0,480]],'T1':[[0,0],[800,0],[800,480],[0,480]]}
            (root/'corners.json').write_text(json.dumps(corners))
            Image.new('RGB',(800,480),'white').save(root/'photo.png')
            run=subprocess.run([sys.executable,'-B',str(Path(__file__).parent/'hil/measure_writer_optical.py'),
                '--stage-a','--registration-reference','fixed','--analysis',str(root/'analysis.json'),
                '--corners',str(root/'corners.json'),'--t0',str(root/'photo.png'),'--t1',str(root/'photo.png'),
                '--output',str(root/'result.json')],capture_output=True,text=True)
            self.assertEqual(run.returncode,2,run.stderr)
            result=json.loads((root/'result.json').read_text())
            self.assertEqual(result['regions'],{})
            self.assertFalse(result['stage_a']['ready_for_review'])
            self.assertFalse(result['metadata'][0]['upsampling_recovers_source_resolution'])
            self.assertEqual(len(result['metadata'][0]['sha256']),64)


if __name__=='__main__':unittest.main()
