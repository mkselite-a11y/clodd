import json
t = json.load(open('tables.json'))
import re
src = open('worker_src.js').read()
# The panel runs the same precheck as the relay, so greyed-out buttons match the server.
pre = re.search(r'^function precheck\(.*?^}\n', src, re.S | re.M).group(0)
pre += re.search(r'^// Harder events.*?^function costFor\(.*?^}\n', src, re.S | re.M).group(0)
panel = open('panel.html').read().replace('__TABLES_JS__', json.dumps(t)).replace('/*__PRECHECK__*/', pre)
out = src.replace('__TABLES__', json.dumps(t)).replace('__PANEL__', json.dumps(panel))
open('worker.js', 'w').write(out)
print(len(out))
