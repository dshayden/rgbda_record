import argparse, numpy as np

def analyzeTs(fname):
  lines = open(fname, 'r').read().split('\n')
  splits = [x.split(', ') for x in lines[6:-1]]
  ts = np.array([[float(x) for x in y] for y in splits])
  
  tsSys = ts[:,2]
  tsRgb = ts[:,0]

  diffs = np.array([x-y for x,y in zip(tsSys[1:], tsSys)])
  # diffs = np.array([x-y for x,y in zip(tsRgb[1:], tsRgb)])

  print('max: %f, idx: %d, line: %d' % (np.max(diffs), np.argmax(diffs), np.argmax(diffs)+7))
  print('min: %f, idx: %d, line: %d' % (np.min(diffs), np.argmin(diffs), np.argmin(diffs)+7))
  print('#>35: %d, #>66: %d' % (np.sum(diffs>35), np.sum(diffs>66)))
  print('#<0: %d, #>100: %d' % (np.sum(diffs<0), np.sum(diffs>100)))
  print('#>200: %d, #>300: %d, #>400: %d, #>500: %d, #>1000: %d' %
    (np.sum(diffs>200), np.sum(diffs>300), np.sum(diffs>400),
    np.sum(diffs>500), np.sum(diffs>1000) ))
  print('median: %f, mean: %f' % (np.median(diffs), np.mean(diffs)))
  print('#frames: %d' % diffs.size)
  print('total time: %d minutes' % (tsSys[-1] / (1000*60)))


if __name__ == "__main__":
  parser = argparse.ArgumentParser(description='Process some integers.')
  parser.add_argument('logfile', metavar='L', type=str, nargs=1,
    help='log file to analyze')
  args = parser.parse_args()

  analyzeTs(args.logfile[0])
