import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
import threading
import yaml

spec=importlib.util.spec_from_file_location('runner',Path(__file__).parents[1]/'scripts/map_runner.py')
runner=importlib.util.module_from_spec(spec);spec.loader.exec_module(runner)

class RunnerContracts(unittest.TestCase):
    def registration(self,root,command,outputs=None):
        value={'schema_version':1,'id':'test.copy','name':'Independent copy plugin',
               'required_inputs':['map_pcd'],'command':command,
               'parameters':{'count':{'type':'int','default':3,'min':1,'max':5}},
               'outputs':outputs or [{'type':'point_cloud','path':'{artifact_dir}/cloud.pcd'}]}
        (root/'algorithm.yaml').write_text(yaml.safe_dump(value))
        return runner.discover([root])['test.copy']

    def test_new_registration_and_parameter_override_without_runner_changes(self):
        with tempfile.TemporaryDirectory(prefix='map runner ') as folder:
            root=Path(folder);(root/'map.pcd').write_text('point data')
            script=root/'plugin.py';script.write_text('import pathlib,shutil,sys\np=pathlib.Path(sys.argv[2]);p.mkdir();shutil.copy2(sys.argv[1],p/"cloud.pcd")\n')
            alg=self.registration(root,[sys.executable,str(script),'{map_pcd}','{artifact_dir}'])
            data=runner.map_inputs(root)
            self.assertEqual(runner.execute(alg,data,root/'job',{'count':'4'}),0)
            result=json.loads((root/'job/result.json').read_text())
            self.assertEqual(result['algorithm_id'],'test.copy')
            self.assertEqual(result['parameters']['count'],4)
            self.assertEqual(Path(result['outputs'][0]['path']).read_text(),'point data')
            with self.assertRaises(FileExistsError): runner.execute(alg,data,root/'job')

    def test_failure_or_missing_artifacts_cannot_claim_success(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);(root/'map.pcd').write_text('data')
            alg=self.registration(root,[sys.executable,'-c','raise SystemExit(7)'])
            self.assertEqual(runner.execute(alg,runner.map_inputs(root),root/'failed'),1)
            self.assertEqual(json.loads((root/'failed/result.json').read_text())['status'],'failed')
            alg['command']=[sys.executable,'-c','pass']
            self.assertEqual(runner.execute(alg,runner.map_inputs(root),root/'missing'),1)

    def test_contract_rejects_missing_inputs_unknown_parameters_and_escape(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            alg=self.registration(root,[sys.executable,'-c','pass'])
            with self.assertRaisesRegex(ValueError,'Missing required'):runner.execute(alg,runner.map_inputs(root),root/'job')
            (root/'map.pcd').write_text('data')
            for overrides in ({'unknown':1},{'count':9}):
                with self.assertRaises(ValueError):runner.execute(alg,runner.map_inputs(root),root/'job',overrides)
            alg['outputs'][0]['path']=str(root/'outside.pcd')
            with self.assertRaisesRegex(ValueError,'inside'):runner.execute(alg,runner.map_inputs(root),root/'job')

    def test_cancel_records_cancelled_and_terminates_child(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);(root/'map.pcd').write_text('data')
            alg=self.registration(root,[sys.executable,'-c','import time; time.sleep(30)'])
            timer=threading.Timer(.2,lambda:runner.cancel(15,None));timer.start()
            try:
                self.assertEqual(runner.execute(alg,runner.map_inputs(root),root/'cancelled'),130)
                result=json.loads((root/'cancelled/result.json').read_text())
                self.assertEqual(result['status'],'cancelled')
            finally:timer.cancel()

    def test_duplicate_ids_are_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);self.registration(root,['program'])
            (root/'duplicate.yaml').write_text((root/'algorithm.yaml').read_text())
            with self.assertRaisesRegex(ValueError,'duplicate'):runner.discover([root])

    def test_check_only_does_not_create_output_and_map_cannot_change_defaults(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);(root/'map.pcd').write_text('data')
            (root/'processing_profile.json').write_text(json.dumps({'package':'.','assets':{},'algorithm_defaults':{'count':99}}))
            alg=self.registration(root,['program'])
            self.assertEqual(runner.execute(alg,runner.map_inputs(root),root/'job',check_only=True),0)
            self.assertFalse((root/'job').exists())
            self.assertEqual(runner.parameters(alg,{})['count'],3)

    def test_records_metadata_and_rejects_inputs_changed_during_run(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);source=root/'map.pcd';source.write_text('data')
            script=root/'plugin.py'
            script.write_text('import pathlib,sys\np=pathlib.Path(sys.argv[2]);p.mkdir();(p/"cloud.pcd").write_text("output")\n')
            alg=self.registration(root,[sys.executable,str(script),'{map_pcd}','{artifact_dir}'])
            self.assertEqual(runner.execute(alg,runner.map_inputs(root),root/'good'),0)
            result=json.loads((root/'good/result.json').read_text())
            self.assertEqual(result['provenance']['inputs']['map_pcd'],runner.input_metadata(source))
            self.assertEqual(result['command'][0],sys.executable)
            script.write_text(script.read_text()+'pathlib.Path(sys.argv[1]).write_text("changed-input")\n')
            self.assertEqual(runner.execute(alg,runner.map_inputs(root),root/'changed'),1)
            self.assertIn('Input files changed',json.loads((root/'changed/result.json').read_text())['error'])

    def test_directory_snapshot_tracks_added_keyframes(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);(root/'0.pcd').write_text('scan')
            before=runner.input_metadata(root)
            (root/'1.pcd').write_text('another scan')
            self.assertNotEqual(before,runner.input_metadata(root))

    def test_unregister_does_not_cancel_an_existing_run(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);(root/'map.pcd').write_text('input')
            script=root/'plugin.py'
            script.write_text('import pathlib,sys\np=pathlib.Path(sys.argv[1]);p.mkdir();'
                              '(p/"cloud.pcd").write_text("output");pathlib.Path(sys.argv[2]).unlink()\n')
            alg=self.registration(root,[sys.executable,str(script),'{artifact_dir}',str(root/'algorithm.yaml')])
            self.assertEqual(runner.execute(alg,runner.map_inputs(root),root/'job'),0)
            self.assertEqual(runner.discover([root]),{})

if __name__=='__main__': unittest.main()
