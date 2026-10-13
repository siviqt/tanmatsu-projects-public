#!/usr/bin/env python3
"""Regenerate public, deterministic codec/HKDF fixtures (requires openssl CLI)."""
import hashlib
import hmac
from pathlib import Path
import subprocess

def sha(b): return hashlib.sha256(b).digest()
def public(scalar):
    der=bytes.fromhex('30310201010420')+scalar.to_bytes(32,'big')+bytes.fromhex('a00a06082a8648ce3d030107')
    return subprocess.run(['openssl','ec','-inform','DER','-pubout','-outform','DER'],input=der,
                          stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=True).stdout[-65:]
pub_a,pub_b=public(1),public(2)
spec=bytes([4,2,1,10,5,5,4,3,3,2,0,0])+(0).to_bytes(4,'big')+(0).to_bytes(4,'big')+bytes(4)
hello=b'SBH\2'+spec+pub_a+bytes(range(16))
ad=b'SBA\2'+spec+sha(hello)+bytes([4])+b'Host'+bytes(12)
reply=b'SBJ\2'+sha(spec)+pub_b+bytes(range(16,32))
join=b'SBC\2'+sha(ad)+sha(reply)
transcript=ad+join+hello+reply
shared=pub_b[1:33] # 1 * (2 * generator), ECDH uses its X coordinate.
prk=hmac.digest(sha(transcript),shared,'sha256'); material=b''; block=b''
for i in range(1,4):
    block=hmac.digest(prk,block+b'SpaceBattles/session/v1'+bytes([i]),'sha256'); material+=block
material=material[:72]
vectors={'spec':spec,'transcript':transcript,'transcript_hash':sha(transcript),'shared':shared,'material':material}
text='// SPDX-License-Identifier: MIT\n// Generated public test fixtures. NEVER use these secrets in an application.\n'
for name,b in vectors.items():
    text+=f'static const uint8_t vector_{name}[{len(b)}]={{\n'
    for at in range(0,len(b),16): text+='    '+','.join(f'0x{x:02x}' for x in b[at:at+16])+',\n'
    text+='};\n'
Path(__file__).with_name('multiplayer-vectors.h').write_text(text)
print('SAS:',int.from_bytes(material[40:44],'big')%1000000)
