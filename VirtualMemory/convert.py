import json
import math
import matplotlib.pyplot as plt
import os
import numpy as np
from os import path, makedirs
from tqdm import tqdm 
import sys

print("Loading files ...")
f = open(sys.argv[1], "r")
print("Finished loading ...")

p = []
r = []
w = []
for line in tqdm(f):
    start = line.split(' ')[0]
    if start != 'Number':
        continue

    pat = line.split(' ')[3]
    value = int(line.split(' ')[-1])
    if pat == 'faults:':
        p.append(value)
    elif pat == 'reads:':
        r.append(value)
    else:
        w.append(value)

pfile = path.join('data/converted', path.split(sys.argv[1])[1][:-4]+'_p.npy')
wfile = path.join('data/converted', path.split(sys.argv[1])[1][:-4]+'_w.npy')
rfile = path.join('data/converted', path.split(sys.argv[1])[1][:-4]+'_r.npy')

np.save(pfile, p)
np.save(wfile, w)
np.save(rfile, r)
