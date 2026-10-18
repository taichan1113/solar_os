from pathlib import Path
base=Path(__file__).resolve().parent
s=(base.parent/'black-hold-d-repeat3/capture_repeat3.py').read_text(encoding='utf-8-sig')
s=s.replace('choices="AD", required=True','choices=("DAA",), default="DAA"')
s=s.replace('display test display0 black-hold-repeat3 {args.condition}', 'display test display0 black-hold-d-prime-aa')
old='if line.startswith("BH phase=") or line.startswith("BH mark=") or line.startswith("BH trace=") or line.startswith("BH trace ") or line.startswith("BH request ") or line.startswith("BH progress "):'
new='if line.startswith(("BH phase=", "BH timing ", "BH lut ", "BH mark=", "BH trace ", "BH request ", "BH schedule ", "BH interval ", "BH progress ")):'
assert old in s;s=s.replace(old,new)
compile(s,'capture_prime_aa.py','exec')
(base/'capture_prime_aa.py').write_text(s,encoding='utf-8',newline='\n')
print('Future capture adapter syntax checked only; no import/execution/port access')
