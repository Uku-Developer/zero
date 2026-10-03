import gzip, struct, tempfile, unittest
from replay_extract import parse, ReplayError
def fixture(events, comment=b'test\0\0'):
    off=92+len(comment); h=bytearray(off); h[:8]=b'asssgame'; struct.pack_into('<IIIIIIqI',h,8,200,off,len(events),0,3,8025,0,0); h[92:]=comment; return bytes(h)+gzip.compress(b''.join(events))
def ev(t, typ, body=b''): return struct.pack('<Ih',t,typ)+body
def packet(extra=False, player=7):
    b=bytearray(32 if extra else 22); b[0]=32 if extra else 22; b[1]=5; struct.pack_into('<IhhBBhhHh',b,2,1234 if player is None else player,-10,20,9,3,100,200,300,400); b[20:22]=bytes((0xA5,0x9E))
    if extra: struct.pack_into('<HHHI',b,22,50,60,70,0x2AAAAA55)
    return bytes(b)
class Tests(unittest.TestCase):
    def parse_events(self,x):
        with tempfile.NamedTemporaryFile(suffix='.rec',delete=False) as f:
            f.write(fixture(x)); p=f.name
        return parse(p)
    def test_header_comment(self): self.assertEqual(self.parse_events([])[0]['comments'],'test')
    def test_wrappers_weapon_extra(self):
        _,e=self.parse_events([ev(1,150,struct.pack('<h',7)+packet(player=1234)),ev(2,151,struct.pack('<h',8)+packet(True,1234))]); self.assertEqual(e[0]['client_time'],1234); self.assertEqual(e[0]['weapon']['type'],5); self.assertEqual(e[1]['extra']['energy'],50)
    def test_legacy_22_32_24(self):
        p24=bytearray(packet()[:22]+b'zz'); p24[0]=24
        _,e=self.parse_events([ev(1,7,packet(player=42)),ev(2,7,packet(True,43)),ev(3,7,bytes(p24))]); self.assertEqual(e[0]['player_id'],42); self.assertIsNone(e[0]['normalized_position']['client_time']); self.assertEqual(e[2]['legacy_tail'],'7a7a')
    def test_lifecycle_kill(self):
        enter=struct.pack('<h24s24shh',1,b'Pilot\0',b'Squad\0',1,2); _,e=self.parse_events([ev(1,1,enter),ev(2,5,struct.pack('<hhhh',1,2,3,4))]); self.assertEqual(e[1]['killer'],1)
    def test_variable_sizes(self):
        brick=bytes((0x2F,))+b'\0'*16
        xs=[ev(1,6,struct.pack('<hBBH',-1,1,0,3)+b'hey'),ev(2,8,struct.pack('<H',3)+b'abc'),ev(3,110,struct.pack('<h',2)+b'abcd'),ev(4,9,brick),ev(5,0)]
        parsed=self.parse_events(xs)[1]; self.assertEqual(len(parsed),5); self.assertEqual(parsed[4]['type'],'Null')
    def test_fail_closed(self):
        for x in (ev(1,32767),ev(1,7,b'\x15'+b'\0'*21),ev(1,5,b'\0'*7),ev(1,8,struct.pack('<H',5)+b'x')):
            with self.assertRaises(ReplayError): self.parse_events([x])
    def test_corrupt_gzip_and_truncated_header(self):
        for data in (b'asssgame'+b'\0'*84+b'bad',b'asssgame'):
            with tempfile.NamedTemporaryFile(delete=False) as f: f.write(data); p=f.name
            with self.assertRaises(ReplayError): parse(p)
if __name__=='__main__': unittest.main()
