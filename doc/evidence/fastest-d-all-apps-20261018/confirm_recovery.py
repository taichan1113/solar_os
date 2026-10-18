from pathlib import Path
import json,re
base=Path(__file__).resolve().parent
a=Path(json.loads((base/'active-flash-archive.json').read_text(encoding='utf-8'))['archive'])
log=(a/'confirmed-runtime-usb.log').read_text(encoding='utf-8',errors='replace')
m=re.search(r'fastest-lut err=0 mode=D scope=all-apps-except-sketch attempts=(\d+) D0x20=(\d+) success=(\d+) error=(\d+) driver_error=(\d+) failed=(\d+)',log)
assert m
attempts,d,success,errors,driver_errors,failed=map(int,m.groups())
assert attempts==d==success and d>0 and errors==driver_errors==failed==0
p=re.search(r'display physical=(\d+) A=(\d+) D=(\d+) OTP=(\d+) noop=(\d+)',log)
assert p and int(p[2])>0 and int(p[3])==d
sketch_a='source=sketch mode=A 0x20=1 err=0' in log
assert 'source=other mode=D 0x20=1 err=0 restored=1' in log
assert 'async enabled=1 busy=0 pending=0 suspended=0 error=0' in log
launcher_at_capture=bool(re.search(r'launcher\s+launcher\s+active',log))
path=a/'user-confirmations.json'
c=json.loads(path.read_text(encoding='utf-8'))
c.update(visual='修正版の表示正常（ユーザー確認）',shell='最終修正版でも入力・削除・表示正常（ユーザー確認）',
 apps='Sketchの黒線・Shell表示・Launcher復帰すべて正常（ユーザー確認）。TypeMouseの最終修正版でのPC操作は再確認していない',
 runtime=f'D {d}回・成功{success}回 / A {p[2]}回 / OTP {p[4]}回、Driverエラー0。Sketch A trace確認={sketch_a}、他アプリD復帰trace正常',
 final_manual_check_answer_received=True,
 final_user_answer='Sketch黒線・Shell表示・Launcher復帰すべて正常')
path.write_text(json.dumps(c,ensure_ascii=False,indent=2),encoding='utf-8')
path=a/'deployment-audit.json';r=json.loads(path.read_text(encoding='utf-8'))
r.update(visual_confirmation='user confirmed normal corrected firmware',
 final_Sketch_black_stroke_visual_confirmation='user confirmed normal black stroke',
 final_user_manual_check_answer_received=True,final_D_updates=d,final_D_success=success,
 final_A_updates=int(p[2]),final_OTP_updates=int(p[4]),final_Driver_errors=0,
 final_Sketch_A_trace_confirmed=sketch_a,final_other_D_restored_trace_confirmed=True,
 final_Launcher_running_at_trace_capture=launcher_at_capture,
 user_confirmed_Launcher_return=True,
 final_async_busy=False,final_async_pending=False,final_async_error=0)
path.write_text(json.dumps(r,indent=2),encoding='utf-8')
print(json.dumps({'D':d,'A':int(p[2]),'Sketch_A_trace':sketch_a,'user_visual':'normal'},indent=2))
