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


if __name__=='__main__':unittest.main()
