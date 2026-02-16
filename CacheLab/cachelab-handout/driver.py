#!/usr/bin/env python3
#
# driver.py - Python 3 compatible version for Cache Lab
#
import subprocess
import re
import os
import sys
import optparse

def computeMissScore(miss, lower, upper, full_score):
    if miss <= lower:
        return full_score
    if miss >= upper:
        return 0
    score = (miss - lower) * 1.0
    range_ = (upper - lower) * 1.0
    return round((1 - score / range_) * full_score, 1)

def main():

    # Configure max scores
    maxscore = {}
    maxscore['csim'] = 27
    maxscore['transc'] = 1
    maxscore['trans32'] = 8
    maxscore['trans64'] = 8
    maxscore['trans60'] = 10

    # Parse the command line arguments
    parser = optparse.OptionParser()
    parser.add_option("-A", action="store_true", dest="autograde",
                      help="emit autoresult string for Autolab")
    opts, args = parser.parse_args()
    autograde = opts.autograde

    # Part A: Check cache simulator correctness
    print("Part A: Testing cache simulator")
    print("Running ./test-csim")
    p = subprocess.Popen("./test-csim", shell=True, stdout=subprocess.PIPE)
    stdout_data = p.communicate()[0].decode('utf-8')  # decode bytes -> str

    # Split lines
    stdout_lines = re.split('\n', stdout_data)
    resultsim = []
    for line in stdout_lines:
        if re.match("TEST_CSIM_RESULTS", line):
            resultsim = re.findall(r'(\d+)', line)
        else:
            print(line)

    # Part B: Check transpose function correctness/performance
    print("Part B: Testing transpose function")

    # Helper function to run test-trans
    def run_trans(M, N):
        cmd = f"./test-trans -M {M} -N {N} | grep TEST_TRANS_RESULTS"
        p = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE)
        stdout_data = p.communicate()[0].decode('utf-8')
        return re.findall(r'(\d+)', stdout_data)

    result32 = run_trans(32, 32)
    print("Running ./test-trans -M 32 -N 32")

    result64 = run_trans(64, 64)
    print("Running ./test-trans -M 64 -N 64")

    result61 = run_trans(60, 68)
    print("Running ./test-trans -M 60 -N 68")

    # Compute scores
    csim_cscore = list(map(int, resultsim[0:1]))
    trans_cscore = int(result32[0]) * int(result64[0]) * int(result61[0])
    miss32 = int(result32[1])
    miss64 = int(result64[1])
    miss61 = int(result61[1])

    trans32_score = computeMissScore(miss32, 300, 600, maxscore['trans32']) * int(result32[0])
    trans64_score = computeMissScore(miss64, 1300, 2000, maxscore['trans64']) * int(result64[0])
    trans61_score = computeMissScore(miss61, 2000, 3000, maxscore['trans60']) * int(result61[0])
    total_score = csim_cscore[0] + trans32_score + trans64_score + trans61_score

    # Summarize
    print("\nCache Lab summary:")
    print("%-22s%8s%10s%12s" % ("", "Points", "Max pts", "Misses"))
    print("%-22s%8.1f%10d" % ("Csim correctness", csim_cscore[0], maxscore['csim']))

    for miss, score, maxpt, label in [(miss32, trans32_score, maxscore['trans32'], "Trans perf 32x32"),
                                      (miss64, trans64_score, maxscore['trans64'], "Trans perf 64x64"),
                                      (miss61, trans61_score, maxscore['trans60'], "Trans perf 60x68")]:
        misses = str(miss)
        if miss == 2**31 - 1:
            misses = "invalid"
        print("%-22s%8.1f%10d%12s" % (label, score, maxpt, misses))

    print("%22s%8.1f%10d" % ("Total points", total_score,
                             maxscore['csim'] + maxscore['trans32'] +
                             maxscore['trans64'] + maxscore['trans60']))

    # Emit autoresult for Autolab
    if autograde:
        autoresult = "%.1f:%d:%d:%d" % (total_score, miss32, miss64, miss61)
        print("\nAUTORESULT_STRING=%s" % autoresult)

if __name__ == "__main__":
    main()
