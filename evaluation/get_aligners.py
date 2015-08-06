#!/usr/bin/env python

import sys, os
use_message = '''
'''

def get_aligners():
    mac = (sys.platform == "darwin")
    if not os.path.exists("aligners"):
        os.mkdir("aligners")
    os.chdir("aligners")
    if not os.path.exists("bin"):
        os.mkdir("bin")
    aligners = ["HISAT", "Bowtie2", "Bowtie", "TopHat2", "STAR", "GSNAP"]
    for aligner in aligners:
        if aligner == "HISAT":
            dir = "hisat-0.1.6-beta"
            if os.path.exists(dir):
                continue
            fname = dir + "-source.zip"
            url = "http://www.ccb.jhu.edu/software/hisat/downloads"
            bins =  "hisat-align-s hisat-build-s hisat-inspect-s"
            installs = bins + " hisat hisat-build hisat-inspect"
            cmd = "wget %s/%s; unzip %s; cd %s; make %s; cp %s ../bin; cd .." % \
                (url, fname, fname, dir, bins, installs)
        elif aligner == "Bowtie2":
            dir = "bowtie2-2.2.5"
            if os.path.exists(dir):
                continue
            fname = dir + "-source.zip"
            url = "http://sourceforge.net/projects/bowtie-bio/files/bowtie2/2.2.5"
            bins = "bowtie2-align-s bowtie2-build-s bowtie2-inspect-s"
            installs = bins + " bowtie2 bowtie2-build bowtie2-inspect"
            cmd = "wget %s/%s; unzip %s; cd %s; make %s; cp %s ../bin; cd .." % \
                (url, fname, fname, dir, bins, installs)
        elif aligner == "Bowtie":
            dir = "bowtie-1.1.2"
            if os.path.exists(dir):
                continue
            fname = dir + "-src.zip"
            url = "http://sourceforge.net/projects/bowtie-bio/files/bowtie/1.1.2"
            bins = "bowtie-align-s bowtie-build-s bowtie-inspect-s"
            installs = bins + " bowtie bowtie-build bowtie-inspect"
            cmd = "wget %s/%s; unzip %s; cd %s; make %s; cp %s ../bin; cd .." % \
                (url, fname, fname, dir, bins, installs)
        elif aligner == "TopHat2":
            if mac:
                dir = "tophat-2.1.0.OSX_x86_64"
            else:
                dir = "tophat-2.1.0.Linux_x86_64"
            if os.path.exists(dir):
                continue
            fname = dir + ".tar.gz"
            url = "http://ccb.jhu.edu/software/tophat/downloads"
            installs = "gtf_juncs juncs_db prep_reads segment_juncs tophat tophat_reports sra_to_solid samtools_0.1.18 map2gtf fix_map_ordering bam_merge long_spanning_reads sam_juncs gtf_to_fasta bam2fastx"
            cmd = "wget %s/%s; tar xvzf %s; cd %s; cp %s ../bin; cd .." % \
                (url, fname, fname, dir, installs)
        elif aligner == "STAR":
            dir = "STAR_2.4.2a"
            if os.path.exists("STAR-" + dir):
                continue
            fname = dir + ".tar.gz"
            url = "https://github.com/alexdobin/STAR/archive"
            if mac:
                add_cmd = "awk '{if($1 ~ /^CXX/) {print \"CXX =/opt/local/bin/g++-mp-4.8\";} else {print;}}' Makefile > Makefile.tmp; mv Makefile.tmp Makefile"
                make_arg = "STARforMac"
                cmd = "wget %s/%s; tar xvzf %s; cd STAR-%s/source; %s; make; make %s; cp STAR ../../bin; cd ../.." % \
                    (url, fname, fname, dir, add_cmd, make_arg)
            else:
                cmd = "wget %s/%s; tar xvzf %s; cd STAR-%s/source; make; cp STAR ../../bin; cd ../.." % \
                    (url, fname, fname, dir)
        elif aligner == "GSNAP":
            dir = "gmap-2015-07-23"
            dir2 = "gmap-gsnap-2015-07-23"
            if os.path.exists(dir):
                continue
            fname = dir2 + ".tar.gz"
            url = "http://research-pub.gene.com/gmap/src"
            installs = "gmap gmapl get-genome gmapindex iit_store iit_get iit_dump gsnap gsnapl uniqscan uniqscanl snpindex cmetindex atoiindex sam_sort ../util/*"
            cmd = "wget %s/%s; tar xvzf %s; cd %s; ./configure; make; cd src; cp %s ../../bin; cd ../.." % \
                (url, fname, fname, dir, installs)
        else:
            assert False
        print >> sys.stderr, cmd
        os.system(cmd)

    files = ["hisat2", "hisat2-align-s", "hisat2-build", "hisat2-build-s", "hisat2-inspect", "hisat2-inspect-s", "extract_splice_sites.py", "extract_snps.py", "simulate_reads.py"]
    os.chdir("bin")
    for file in files:
        if os.path.exists(file):
            continue
        os.system("ln -s ../../../%s %s" % (file, file))
    os.chdir("..")
    
    os.chdir("..")
            
    
if __name__ == "__main__":
    get_aligners()
