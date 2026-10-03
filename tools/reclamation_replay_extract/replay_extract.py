#!/usr/bin/env python3
"""Stdlib-only extractor for Reclamation/ASSS .rec files."""
import argparse, gzip, json, struct, sys, zlib
from pathlib import Path

HEADER = 92
ET = {0:'Null',1:'Enter',2:'Leave',3:'ShipChange',4:'FreqChange',5:'Kill',6:'Chat',7:'Position',8:'Packet',9:'Brick',10:'BallFire',11:'BallCatch',12:'BallPacket',13:'BallGoal',14:'ArenaMessage',100:'CrownToggleOn',101:'CrownToggleOff',110:'StaticFlagFullUpdate',111:'StaticFlagClaimed',120:'CarryFlagGameReset',121:'CarryFlagOnMap',122:'CarryFlagPickup',123:'CarryFlagDrop',130:'SecuritySeedChange',140:'AttachChange',141:'TurretKickoff',150:'PositionWrapper',151:'PositionWithExtraWrapper'}
FIXED = {'Null':6,'Leave':8,'ShipChange':12,'FreqChange':10,'Kill':14,'Brick':23,'BallFire':6,'BallCatch':6,'BallPacket':22,'BallGoal':6,'ArenaMessage':6,'CrownToggleOn':8,'CrownToggleOff':8,'StaticFlagClaimed':10,'CarryFlagGameReset':12,'CarryFlagOnMap':16,'CarryFlagPickup':10,'CarryFlagDrop':8,'SecuritySeedChange':18,'AttachChange':10,'TurretKickoff':8,'PositionWrapper':30,'PositionWithExtraWrapper':40}
class ReplayError(Exception): pass
def u16(b,o): return struct.unpack_from('<H',b,o)[0]
def i16(b,o): return struct.unpack_from('<h',b,o)[0]
def u32(b,o): return struct.unpack_from('<I',b,o)[0]
def text0(b): return b.split(b'\0',1)[0].decode('latin-1', 'replace')
def header(raw, source):
    if len(raw)<HEADER: raise ReplayError('truncated file header')
    if raw[:8] != b'asssgame': raise ReplayError('invalid header magic (expected asssgame)')
    off=u32(raw,12)
    if off < HEADER or off > len(raw): raise ReplayError(f'invalid event offset {off}')
    return {'source':str(source),'format_magic':'asssgame','version':u32(raw,8),'event_offset':off,'declared_events':u32(raw,16),'end_time':u32(raw,20),'max_player_id':u32(raw,24),'spectator_freq':u32(raw,28),'recorded_unix':struct.unpack_from('<q',raw,32)[0],'map_checksum':u32(raw,40),'recorder':text0(raw[44:68]),'arena':text0(raw[68:92]),'comments':text0(raw[HEADER:off])}
def position(b, extra=False, legacy=False):
    # C2S_PositionPacket: type, rotation, client time, x-speed, y, checksum, status, x, y-speed, bounty, energy, weapon[2]
    base=0
    d={'rotation':struct.unpack_from('<b',b,base+1)[0],'x_speed':i16(b,base+6),'y':i16(b,base+8),'status':b[base+11],'x':i16(b,base+12),'y_speed':i16(b,base+14),'bounty':u16(b,base+16),'energy':i16(b,base+18)}
    if legacy:
        d['player_id']=u32(b,base+2); d['client_time_available']=False
    else:
        d['client_time']=u32(b,base+2); d['client_time_available']=True
    w1,w2=b[base+20],b[base+21]; d['weapon']={'type':w1&31,'level':(w1>>5)&3,'shrap_bouncing':bool(w1&128),'shrap_level':w2&3,'shrap':(w2>>2)&31,'alternate':bool(w2&128)}
    if extra:
        e=base+22; bits=u32(b,e+6); d['extra']={'energy':u16(b,e),'s2c_ping':u16(b,e+2),'timer':u16(b,e+4),'shields':bool(bits&1),'super':bool(bits&2),'bursts':(bits>>2)&15,'repels':(bits>>6)&15,'thors':(bits>>10)&15,'bricks':(bits>>14)&15,'decoys':(bits>>18)&15,'rockets':(bits>>22)&15,'portals':(bits>>26)&15}
    return d
def events(raw, start, source):
    p=start; idx=0; out=[]; unsupported=[]
    while p < len(raw):
        if len(raw)-p<6: raise ReplayError(f'truncated event header at byte {p}')
        tick=u32(raw,p); code=struct.unpack_from('<h',raw,p+4)[0]; name=ET.get(code)
        if name is None: raise ReplayError(f'unsupported event type {code} at event {idx}, byte {p}')
        n=FIXED.get(name)
        if name=='Enter': n=60
        elif name=='Chat':
            if len(raw)-p<12: raise ReplayError(f'truncated Chat header at event {idx}')
            n=12+u16(raw,p+10)
        elif name=='Position':
            if len(raw)-p<7: raise ReplayError(f'truncated Position length at event {idx}')
            length=raw[p+6]
            if length not in (22,24,32): raise ReplayError(f'unsupported legacy Position packet length {length} at event {idx}')
            n=6+length
        elif name=='Packet': n=8+(u16(raw,p+6) if len(raw)-p>=8 else (_ for _ in ()).throw(ReplayError('truncated Packet header')))
        elif name=='StaticFlagFullUpdate':
            if len(raw)-p<8: raise ReplayError('truncated StaticFlagFullUpdate header')
            count=i16(raw,p+6)
            if count<0: raise ReplayError('negative static flag count')
            n=8+2*count
        if n is None: raise ReplayError(f'no sizing rule for {name}')
        if p+n>len(raw): raise ReplayError(f'truncated {name} event {idx}: need {n} bytes')
        b=raw[p:p+n]; d={'source':str(source),'event_index':idx,'server_tick':tick,'type':name}
        if name in ('Enter','Leave','ShipChange','FreqChange','PositionWrapper','PositionWithExtraWrapper'): d['player_id']=i16(b,6)
        if name=='Enter': d.update(name=text0(b[8:32]), squad=text0(b[32:56]), ship=i16(b,56), freq=i16(b,58))
        elif name=='Leave': pass
        elif name=='ShipChange': d.update(ship=i16(b,8),freq=i16(b,10))
        elif name=='FreqChange': d['freq']=i16(b,8)
        elif name=='Kill': d.update(killer=i16(b,6),killed=i16(b,8),points=i16(b,10),flags=i16(b,12))
        elif name=='Position':
            d.update(position(b[6:6+raw[p+6]], raw[p+6]==32, legacy=True))
            if raw[p+6]==24: d['legacy_tail']=b[6+22:6+24].hex()
            d['normalized_position']={'player_id':d['player_id'],'server_tick':tick,'client_time':None,'x':d['x'],'y':d['y'],'x_speed':d['x_speed'],'y_speed':d['y_speed'],'rotation':d['rotation']}
        elif name.startswith('Position'):
            d.update(position(b[8:], name.endswith('WithExtraWrapper'))); d['normalized_position']={'player_id':d['player_id'],'server_tick':tick,'client_time':d['client_time'],'x':d['x'],'y':d['y'],'x_speed':d['x_speed'],'y_speed':d['y_speed'],'rotation':d['rotation']}
        out.append(d); p+=n; idx+=1
    return out
def parse(path):
    raw=Path(path).read_bytes(); h=header(raw,path)
    try: stream=gzip.decompress(raw[h['event_offset']:])
    except (OSError, EOFError, zlib.error) as e: raise ReplayError(f'invalid gzip event stream: {e}')
    ev=events(stream,0,path); h['actual_events']=len(ev); return h,ev
def main():
    ap=argparse.ArgumentParser(); ap.add_argument('file'); ap.add_argument('--mode',choices=['jsonl','summary'],default='jsonl'); a=ap.parse_args()
    try: h,ev=parse(a.file)
    except (OSError,ReplayError) as e: print(f'error: {e}',file=sys.stderr); return 2
    if a.mode=='summary': print(json.dumps({'header':h,'event_count':len(ev),'types':{k:sum(x['type']==k for x in ev) for k in sorted(set(x['type'] for x in ev))}},sort_keys=True))
    else:
        print(json.dumps({'record_type':'header',**h},sort_keys=True))
        for x in ev: print(json.dumps(x,sort_keys=True))
    return 0
if __name__=='__main__': sys.exit(main())
