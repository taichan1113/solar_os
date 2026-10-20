import importlib.util
from pathlib import Path
import struct
import unittest
import zlib

spec=importlib.util.spec_from_file_location('audit',Path(__file__).resolve().parents[1]/'scripts/papermono_e1_flash_audit.py')
audit=importlib.util.module_from_spec(spec); spec.loader.exec_module(audit)

class OfflineAuditTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        entries=[(1,2,0x9000,0x4000,b'nvs'),(1,0,0xd000,0x2000,b'otadata'),
                 (0,0x10,0x10000,0x700000,b'ota_0'),(0,0x11,0x710000,0x700000,b'ota_1'),
                 (1,0x81,0xe10000,0x1f0000,b'flash')]
        cls.table=b''.join(struct.pack('<HBBII16sI',0x50aa,*e,0) for e in entries)+b'\xff'*32
        data=bytearray(b'\xff'*(16*1024*1024)); data[0x8000:0x8000+len(cls.table)]=cls.table
        struct.pack_into('<I',data,0xd000,1); struct.pack_into('<II',data,0xd000+24,2,zlib.crc32(data[0xd000:0xd004],0xffffffff)&0xffffffff)
        cls.full=bytes(data); cls.image=bytearray(8192); cls.image[0]=0xe9; struct.pack_into('<I',cls.image,32,0xabcd5432)
        cls.image=bytes(cls.image)
        cls.log='0 shell shell suspended display0\n1 cdc0/dumb shell active cdc0\nRunning: ota_0 (slot 0) addr 0x10000\nBoot: ota_0 (slot 0) addr 0x10000\n'
    def plan(self,**kw):
        args=dict(full=self.full,table=self.table,image=self.image,preflight=self.log); args.update(kw)
        return audit.make_plan(**args)
    def test_plan_and_protection(self):
        p=self.plan(); self.assertEqual(p['app_offset'],0x10000)
        after=bytearray(self.full); after[0x10000:0x12000]=self.image
        self.assertTrue(audit.verify(p,self.full,bytes(after),self.image)['all_bytes_outside_active_application_unchanged'])
        after[0x9000]^=1
        with self.assertRaises(ValueError):audit.verify(p,self.full,bytes(after),self.image)
    def test_slot_table_or_session_mismatch(self):
        for changes in [dict(preflight=self.log.replace('Boot: ota_0 (slot 0) addr 0x10000','Boot: ota_1 (slot 1) addr 0x710000')),
                        dict(table=b'wrong'),dict(preflight=self.log+'2 memo writer suspended display0\n'),
                        dict(image=b'factory'),dict(full=self.full[:1000])]:
            with self.subTest(changes=list(changes)),self.assertRaises(ValueError):self.plan(**changes)
    def test_titles_with_spaces_cannot_hide_open_documents(self):
        for line in ('1 writer untit writer active display0 40/40ms',
                     '2 Sketch * - u sketch suspended display0 40/30ms',
                     '3 My saved memo writer suspended display0',
                     '4\tSketch * - u\tsketch\tactive\tdisplay0'):
            with self.subTest(line=line), self.assertRaises(ValueError):
                self.plan(preflight=self.log+line+'\n')
        # Multiple words in a display Shell title must not hide a valid Shell.
        self.plan(preflight=self.log.replace('0 shell shell suspended',
                                             '0 Built in shell shell suspended'))

    def test_crc_and_readback_mismatch(self):
        bad=bytearray(self.full);bad[0xd000]^=1
        with self.assertRaises(ValueError):self.plan(full=bytes(bad))
        with self.assertRaises(ValueError):audit.verify(self.plan(),self.full,self.full,self.image)

if __name__=='__main__':unittest.main()
