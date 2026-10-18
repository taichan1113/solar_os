from pathlib import Path
import hashlib,json,subprocess,sys
base=Path(__file__).resolve().parent
before=json.loads((base/'worktrees-before.json').read_text(encoding='utf-8'))
expected=sys.argv[1] if len(sys.argv)>1 else '391925abff2d261d1d5856711aa0a1f101ac435a'
results=[]
for item in before:
 path=Path(item['path'])
 def git(*args):return subprocess.check_output(['git','-C',str(path),*args],text=True,encoding='utf-8').strip()
 current={'head':git('rev-parse','HEAD'),'branch':git('symbolic-ref','HEAD'),
          'status':git('status','--porcelain=v1').splitlines()}
 hashes={name:hashlib.sha256((path/name).read_bytes()).hexdigest() for name in item['changed_file_hashes']}
 is_integration=item['branch']=='refs/heads/integration/papermono-dirty-sketch'
 unchanged=all(current[key]==item[key] for key in current) and hashes==item['changed_file_hashes']
 allowed=(is_integration and current['head']==expected and current['branch']==item['branch'] and
          current['status']==item['status']==[] and hashes==item['changed_file_hashes'])
 if allowed:
  subprocess.run(['git','-C',str(path),'merge-base','--is-ancestor',item['head'],current['head']],check=True)
 ok=allowed if is_integration else unchanged
 results.append({'path':str(path),**current,'changed_file_hashes':hashes,
                 'integration_authorized_update':is_integration,'unchanged':unchanged,'verified':ok})
report={'existing_worktree_count':len(results),'expected_integration_HEAD':expected,
        'all_verified':all(r['verified'] for r in results),'worktrees':results}
(base/'worktrees-after.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
assert report['all_verified'],[r for r in results if not r['verified']]
print(json.dumps({k:v for k,v in report.items() if k!='worktrees'}))
