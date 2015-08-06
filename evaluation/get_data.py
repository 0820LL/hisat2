#!/usr/bin/env python

import sys, os
use_message = '''
'''

def get_data():
    data_root = "http://www.ccb.jhu.edu/software/hisat2/downloads/evaluation"
    
    # Download the reference human genome, SNPs, and gene annotations
    if not os.path.exists("data"):
        os.mkdir("data")
    os.chdir("data")
    files = ["genome.fa", "genome.fa.fai", "genes.gtf", "snpCommon.txt", "genome.snp", "genome.ss", \
                 "22.fa", "22.fa.fai", "genes_22.gtf", "22.snp", "22.ss", \
                 "22_20-21M.fa", "22_20-21M.fa.fai", "genes_22_20-21M.gtf", "22_20-21M.snp", "22_20-21M.ss"]
    for file in files:
        if os.path.exists(file):
            continue
        wget_cmd = "wget %s/data/%s" % (data_root, file)
        print >> sys.stderr, wget_cmd
        os.system(wget_cmd)
    os.chdir("..")

    # Download indexes
    if not os.path.exists("indexes"):
        os.mkdir("indexes")
    os.chdir("indexes")
    aligners = ["HISAT2", "HISAT", "Bowtie", "STAR", "GSNAP"]
    for aligner in aligners:
        files = []
        if aligner == "HISAT2":
            for i in range(8):
                files.append("genome.%d.ht2" % (i+1))
                files.append("genome_snp.%d.ht2" % (i+1))
                files.append("genome_ss.%d.ht2" % (i+1))
                files.append("genome_snp_ss.%d.ht2" % (i+1))
        elif aligner == "HISAT":
            for i in range(6):
                files.append("genome.%d.bt2" % (i+1))
                if not i in [2,3]:
                    files.append("genome.rev.%d.bt2" % (i+1))
        elif aligner == "Bowtie":
            for i in range(4):
                files.append("genome.%d.ebwt" % (i+1))
            files.append("genome.rev.1.ebwt")
            files.append("genome.rev.2.ebwt")
        elif aligner == "STAR":
            files = [
                "Genome", 
                "SA",
                "SAindex",
                "chrLength.txt",
                "chrName.txt",
                "chrNameLength.txt",
                "chrStart.txt",
                "genomeParameters.txt"
                ]
        elif aligner == "GSNAP":
            files = [
                "genome",
                "splicesites.iit"
                ]
        else:
            assert False

        if not os.path.exists(aligner):
            os.mkdir(aligner)
        os.chdir(aligner)
        for file in files:
            if os.path.exists(file):
                continue            
            wget_cmd = "wget %s/indexes/%s/%s" % (data_root, aligner, file)
            print >> sys.stderr, wget_cmd
            os.system(wget_cmd)
        os.chdir("..")
    os.chdir("..")
            
    
if __name__ == "__main__":
    get_data()
