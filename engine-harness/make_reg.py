"""Generate a Wine .reg file with the matcher's tuning values, read from the
vendor .inf you supply (vendor files are not distributed with this project).

usage: python3 make_reg.py vendor/FtWbioDriverUmdfV3.inf focal.reg
"""
import re
import sys

text = open(sys.argv[1], "rb").read().decode("utf-16")
vals = re.findall(r'HKLM,\s*%ServiceRoot%"\\focalFp",(\w+),%REG_DWORD%,(\d+)', text)
lines = ["Windows Registry Editor Version 5.00", "",
         r"[HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\focalFp]"]
lines += [f'"{k}"=dword:{int(v):08x}' for k, v in vals]
open(sys.argv[2], "w").write("\r\n".join(lines) + "\r\n")
print(len(vals), "registry values written")
