import importlib.util
import json
from pathlib import Path
import tempfile
import subprocess
import sys
import unittest
import numpy as np

spec = importlib.util.spec_from_file_location('pipeline', Path(__file__).parents[1]/'scripts/run_pipeline.py')
pipeline = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pipeline)

class AlgorithmContracts(unittest.TestCase):
    def test_isolated_obstacle_is_removed_but_parking_and_non_candidate_survive(self):
        xyz = np.array([[0,0,1],[5,5,1],[10,10,0]],float)
        rejected = pipeline.radius_mask(xyz,np.array([1,1,0],bool),np.zeros(3,bool),
             {'parked_car_mask':np.array([0,1,0])},.2,5)
        np.testing.assert_array_equal(rejected,[True,False,False])

    def test_neighbor_threshold_includes_self_and_boundary(self):
        xyz = np.array([[0,0,1],[.2,0,1]],float)
        rejected = pipeline.radius_mask(xyz,np.ones(2,bool),np.zeros(2,bool),
             {'parked_car_mask':np.zeros(2)},.2,2)
        self.assertFalse(rejected.any())

    def test_no_candidates(self):
        mask=pipeline.radius_mask(np.empty((0,3)),np.zeros(0,bool),np.zeros(0,bool),
             {'parked_car_mask':np.zeros(0)},.2,5)
        self.assertEqual(len(mask),0)

    def test_octomap_requires_all_heights_frames_and_preserves_obstacles(self):
        before=np.array([[0,205,205,205,254]],np.uint8)
        after,new=pipeline.fuse_free(before,np.ones_like(before,bool),np.zeros_like(before),
             np.ones_like(before),np.array([[8,8,7,8,8]]),np.zeros_like(before),
             np.array([[3,3,3,2,3]]),.1,3)
        np.testing.assert_array_equal(after,[[0,254,205,205,254]])
        self.assertEqual(int(new.sum()),1)

    def test_profile_without_hashes_and_missing_files(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            assets={key:f'{key}.dat' for key in ('detections','source','baseline','parked','evidence','tracks')}
            for filename in [*assets.values(),'map.pcd','poses.txt','poses_timed.txt','metadata.yaml']:
                (root/filename).write_text('data')
            (root/'patches').mkdir()
            (root/'patches/0.pcd').write_text('scan')
            path=root/'processing_profile.json'
            profile={'package':'.','assets':assets, 'centerpoint_mode':'wrong',
                     'algorithm_defaults':{'radius':999}}
            path.write_text(json.dumps(profile))
            loaded=pipeline.load_profile(path)
            self.assertEqual(loaded['package'],str(root))
            self.assertNotIn('centerpoint_mode',loaded)
            self.assertNotIn('algorithm_defaults',loaded)
            pipeline.validate(loaded,root)
            # Editing input and stale legacy hashes must not reject the map.
            (root/'map.pcd').write_text('edited map')
            profile['sha256']={'map.pcd':'wrong'}
            path.write_text(json.dumps(profile))
            pipeline.validate(pipeline.load_profile(path),root)
            (root/'detections.dat').unlink()
            with self.assertRaisesRegex(ValueError,'missing'):
                pipeline.validate(loaded,root)

    def test_prepare_rebases_paths_and_never_overwrites(self):
        prepare = pipeline.module('prepare', 'prepare_map.py').prepare
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            (root/'map').mkdir()
            source=root/'profile.json'
            source.write_text(json.dumps({'package':'map', 'assets':{'source':'map/cloud.pcd'},
                                        'sha256':{'map/cloud.pcd':'hash'}, 'centerpoint_mode':'old',
                                        'algorithm_defaults':{'radius':999},
                                        'coordinate_system':{'frame_id':'map'}}))
            output=root/'map/processing_profile.json'
            prepare(source,output)
            migrated=json.loads(output.read_text())
            self.assertNotIn('sha256', migrated)
            self.assertNotIn('centerpoint_mode', migrated)
            self.assertNotIn('algorithm_defaults', migrated)
            self.assertEqual(migrated['coordinate_system'], {'frame_id':'map'})
            self.assertEqual(migrated['package'], '.')
            self.assertEqual(migrated['assets']['source'], 'cloud.pcd')
            with self.assertRaises(FileExistsError):
                prepare(source,output)

    def test_cancel_stops_active_algorithm_child(self):
        child=subprocess.Popen([sys.executable,'-c','import time; time.sleep(60)'])
        pipeline.CHILD=child
        try:
            with self.assertRaises(SystemExit) as stopped:
                pipeline.cancel(15,None)
            self.assertEqual(stopped.exception.code,130)
            self.assertIsNotNone(child.poll())
        finally:
            pipeline.CHILD=None
            if child.poll() is None:
                child.kill()
            child.wait()

if __name__=='__main__':
    unittest.main()
