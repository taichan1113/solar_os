from pathlib import Path
import hashlib,json
base=Path(__file__).resolve().parent
old=base.parent/'writer-d-integration'
m=json.loads((base/'artifacts/manifest.json').read_text(encoding='utf-8'))
assert m['result']=='ALL_APP_D_SKETCH_BASELINE_HOST_BUILD_PASS' and m['scope']=='all-apps-except-sketch'
image=(base/'artifacts/firmware-app-only.bin').read_bytes()
assert hashlib.sha256(image).hexdigest()==m['sha256']
archive=Path(json.loads((base/'active-flash-archive.json').read_text(encoding='utf-8'))['archive'])
s=(old/'flash_writer_guarded.py').read_text(encoding='utf-8')
s=s.replace("manifest=json.loads((base/'artifacts/manifest.json').read_text())",
            "manifest=json.loads((base/'artifacts/manifest.json').read_text(encoding='utf-8'))\nassert manifest['scope']=='all-apps-except-sketch' and manifest['result']=='ALL_APP_D_SKETCH_BASELINE_HOST_BUILD_PASS'")
s=s.replace("=='6e3c6a02b701bf5b5b41a02c36e767ce7dbd4efdda338223c1f7f93f689551a1'", "=="+repr(m['sha256']))
s=s.replace('==3459904','=='+str(m['application_bytes']))
s=s.replace("base.parent/'writer-black-hold-d/artifacts/firmware-app-only.bin'",
            "base/'attempt-all-apps-initial/firmware-app-only.bin'")
s=s.replace("assert sha(previous_trial)=='517250fa47fd1d702fccf28e5f3da6d4557fca978bd6f9e953d589ed6b29a253'",
            "assert sha(previous_trial)=='769dc0f5d835f70009ea054a021ae1bb5546d7a909a069c1dfd7f53f204375e4'")
s=s.replace("writer_default_mode='D',cumulative_limit=None", "default_mode='D',scope='all-apps-except-sketch',cumulative_limit=None")
compile(s,str(base/'flash_writer_guarded.py'),'exec')
(base/'flash_writer_guarded.py').write_text(s,encoding='utf-8')
# The restore helper is pinned to THIS fresh archive and the old Writer-only app.
s=(old/'restore_writer_guarded.py').read_text(encoding='utf-8')
start=s.index(';archive=Path(');end=s.index('\n',start)
s=s[:start]+';archive=Path('+repr(str(archive))+')'+s[end:]
compile(s,str(base/'restore_writer_guarded.py'),'exec')
(base/'restore_writer_guarded.py').write_text(s,encoding='utf-8')
(archive/'user-authorization.json').write_text(json.dumps({
 'approval':'User explicitly requested flashing all-app version and generating report after flashing',
 'readiness':'USB connected, no unsaved edits, no competing session, display observable',
 'scope':'Application-only display-wide fastest Candidate D Integration'},indent=2),encoding='utf-8')
for name in ('flash_writer_guarded.py','restore_writer_guarded.py','device_query.py'):
 (archive/name).write_bytes((base/name).read_bytes())
print('Fresh-backup guarded flash/restore prepared; candidate SHA '+m['sha256'])
