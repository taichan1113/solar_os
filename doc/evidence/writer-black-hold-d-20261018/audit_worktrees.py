from pathlib import Path
import hashlib,json,subprocess

base=Path(__file__).resolve().parent
before=json.loads((base/'other-worktrees-before.json').read_text(encoding='utf-8-sig'))
results=[]
for item in before:
    path=Path(item['path'])
    def git(*args):
        return subprocess.check_output(['git','-C',str(path),*args],text=True).strip()
    current={'head':git('rev-parse','HEAD'),'branch':git('symbolic-ref','HEAD'),
             'status':git('status','--porcelain=v1').splitlines()}
    hashes={name:hashlib.sha256((path/name).read_bytes()).hexdigest()
            for name in item['changed_file_hashes']}
    unchanged=all(current[key]==item[key] for key in current) and hashes==item['changed_file_hashes']
    results.append({'path':str(path),**current,'changed_file_hashes':hashes,'unchanged':unchanged})
out={'existing_worktree_count':len(results),'all_unchanged':all(r['unchanged'] for r in results),'worktrees':results}
(base/'other-worktrees-after.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'existing_worktree_count':len(results),'all_unchanged':out['all_unchanged']}))
assert out['all_unchanged'],[r for r in results if not r['unchanged']]
