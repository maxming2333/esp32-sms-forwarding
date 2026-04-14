import os
import gzip
import json

data_dir = 'data'
originals = {}
os.makedirs('.pio', exist_ok=True)
for fname in os.listdir(data_dir):
    if fname.endswith('.gz'):
        continue
    fpath = os.path.join(data_dir, fname)
    if not os.path.isfile(fpath):
        continue
    with open(fpath, 'rb') as f:
        content = f.read()
    originals[fname] = list(content)
    gz_path = fpath + '.gz'
    with gzip.open(gz_path, 'wb') as gz_f:
        gz_f.write(content)
    os.remove(fpath)
with open('.pio/originals.json', 'w') as jf:
    json.dump(originals, jf)
