/*
 * Copyright 2015, Daehwan Kim <infphilo@gmail.com>
 *
 * This file is part of HISAT 2.
 *
 * HISAT 2 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * HISAT 2 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with HISAT 2.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <iostream>
#include <fstream>
#include <string>
#include <cassert>
#include <getopt.h>
#include "assert_helpers.h"
#include "endian_swap.h"
#include "formats.h"
#include "sequence_io.h"
#include "tokenize.h"
#include "timer.h"
#include "ref_read.h"
#include "filebuf.h"
#include "reference.h"
#include "ds.h"
#include "gfm.h"
#include "hgfm.h"

/**
 * \file Driver for the bowtie-build indexing tool.
 */

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

// Build parameters
int verbose;
static int sanityCheck;
static int format;
static TIndexOffU bmax;
static TIndexOffU bmaxMultSqrt;
static uint32_t bmaxDivN;
static int dcv;
static int noDc;
static int entireSA;
static int seed;
static int showVersion;
//   GFM parameters
static int32_t lineRate;
static int32_t linesPerSide;
static int32_t offRate;
static int32_t ftabChars;
static int32_t localOffRate;
static int32_t localFtabChars;
static int  bigEndian;
static bool nsToAs;
static bool autoMem;
static bool packed;
static bool writeRef;
static bool justRef;
static bool reverseEach;
static int nthreads;      // number of pthreads operating concurrently
static string wrapper;
static string snp_fname;
static string ss_fname;
static string sv_fname;

static void resetOptions() {
	verbose        = true;  // be talkative (default)
	sanityCheck    = 0;     // do slow sanity checks
	format         = FASTA; // input sequence format
	bmax           = OFF_MASK; // max blockwise SA bucket size
	bmaxMultSqrt   = OFF_MASK; // same, as multplier of sqrt(n)
	bmaxDivN       = 4;          // same, as divisor of n
	dcv            = 1024;  // bwise SA difference-cover sample sz
	noDc           = 0;     // disable difference-cover sample
	entireSA       = 0;     // 1 = disable blockwise SA
	seed           = 0;     // srandom seed
	showVersion    = 0;     // just print version and quit?
	// GFM parameters
	lineRate       = GFM<TIndexOffU>::default_lineRate;
	linesPerSide   = 1;  // 1 64-byte line on a side
	offRate        = 4;  // sample 1 out of 16 SA elts
	ftabChars      = 10; // 10 chars in initial lookup table
    localOffRate   = 3;
    localFtabChars = 6;
	bigEndian      = 0;  // little endian
	nsToAs         = false; // convert reference Ns to As prior to indexing
	autoMem        = true;  // automatically adjust memory usage parameters
	packed         = false; //
	writeRef       = true;  // write compact reference to .3.bt2/.4.bt2
	justRef        = false; // *just* write compact reference, don't index
	reverseEach    = false;
    nthreads       = 1;
    wrapper.clear();
    snp_fname = "";
    ss_fname = "";
    sv_fname = "";
}

// Argument constants for getopts
enum {
	ARG_BMAX = 256,
	ARG_BMAX_MULT,
	ARG_BMAX_DIV,
	ARG_DCV,
	ARG_SEED,
	ARG_CUTOFF,
	ARG_PMAP,
	ARG_NTOA,
	ARG_USAGE,
	ARG_REVERSE_EACH,
    ARG_SA,
	ARG_WRAPPER,
    ARG_LOCAL_OFFRATE,
    ARG_LOCAL_FTABCHARS,
    ARG_SNP,
    ARG_SPLICESITE,
    ARG_SV,
};

/**
 * Print a detailed usage message to the provided output stream.
 */
static void printUsage(ostream& out) {
	out << "HISAT2 version " << string(HISAT2_VERSION).c_str() << " by Daehwan Kim (infphilo@gmail.com, http://www.ccb.jhu.edu/people/infphilo)" << endl;
    
#ifdef BOWTIE_64BIT_INDEX
	string tool_name = "hisat2-build-l";
#else
	string tool_name = "hisat2-build-s";
#endif
	if(wrapper == "basic-0") {
		tool_name = "hisat2-build";
	}
    
	out << "Usage: hisat2-build [options]* <reference_in> <bt2_index_base>" << endl
	    << "    reference_in            comma-separated list of files with ref sequences" << endl
	    << "    hisat2_index_base          write " << gfm_ext << " data to files with this dir/basename" << endl
        << "Options:" << endl
        << "    -c                      reference sequences given on cmd line (as" << endl
        << "                            <reference_in>)" << endl;
    if(wrapper == "basic-0") {
        out << "    --large-index           force generated index to be 'large', even if ref" << endl
		<< "                            has fewer than 4 billion nucleotides" << endl;
	}
    out << "    -a/--noauto             disable automatic -p/--bmax/--dcv memory-fitting" << endl
	    << "    -p                      number of threads" << endl
	    << "    --bmax <int>            max bucket sz for blockwise suffix-array builder" << endl
	    << "    --bmaxdivn <int>        max bucket sz as divisor of ref len (default: 4)" << endl
	    << "    --dcv <int>             diff-cover period for blockwise (default: 1024)" << endl
	    << "    --nodc                  disable diff-cover (algorithm becomes quadratic)" << endl
	    << "    -r/--noref              don't build .3/.4.bt2 (packed reference) portion" << endl
	    << "    -3/--justref            just build .3/.4.bt2 (packed reference) portion" << endl
	    << "    -o/--offrate <int>      SA is sampled every 2^offRate BWT chars (default: 5)" << endl
	    << "    -t/--ftabchars <int>    # of chars consumed in initial lookup (default: 10)" << endl
        << "    --localoffrate <int>    SA (local) is sampled every 2^offRate BWT chars (default: 3)" << endl
        << "    --localftabchars <int>  # of chars consumed in initial lookup in a local index (default: 6)" << endl
        << "    --snp <filename>        SNP file name" << endl
        << "    --ss <filename>         Splice site file name" << endl
	    << "    --seed <int>            seed for random number generator" << endl
	    << "    -q/--quiet              verbose output (for debugging)" << endl
	    << "    -h/--help               print detailed description of tool and its options" << endl
	    << "    --usage                 print this usage message" << endl
	    << "    --version               print version information and quit" << endl
	    ;
    
    if(wrapper.empty()) {
		cerr << endl
        << "*** Warning ***" << endl
        << "'" << tool_name << "' was run directly.  It is recommended "
        << "that you run the wrapper script 'hisat2-build' instead."
        << endl << endl;
	}
}

static const char *short_options = "qrap:h?nscfl:i:o:t:h:3C";

static struct option long_options[] = {
	{(char*)"quiet",          no_argument,       0,            'q'},
	{(char*)"sanity",         no_argument,       0,            's'},
	{(char*)"threads",        required_argument, 0,            'p'},
	{(char*)"little",         no_argument,       &bigEndian,   0},
	{(char*)"big",            no_argument,       &bigEndian,   1},
	{(char*)"bmax",           required_argument, 0,            ARG_BMAX},
	{(char*)"bmaxmultsqrt",   required_argument, 0,            ARG_BMAX_MULT},
	{(char*)"bmaxdivn",       required_argument, 0,            ARG_BMAX_DIV},
	{(char*)"dcv",            required_argument, 0,            ARG_DCV},
	{(char*)"nodc",           no_argument,       &noDc,        1},
	{(char*)"seed",           required_argument, 0,            ARG_SEED},
	{(char*)"entiresa",       no_argument,       &entireSA,    1},
	{(char*)"version",        no_argument,       &showVersion, 1},
	{(char*)"noauto",         no_argument,       0,            'a'},
	{(char*)"noblocks",       required_argument, 0,            'n'},
	{(char*)"linerate",       required_argument, 0,            'l'},
	{(char*)"linesperside",   required_argument, 0,            'i'},
	{(char*)"offrate",        required_argument, 0,            'o'},
	{(char*)"ftabchars",      required_argument, 0,            't'},
    {(char*)"localoffrate",   required_argument, 0,            ARG_LOCAL_OFFRATE},
	{(char*)"localftabchars", required_argument, 0,            ARG_LOCAL_FTABCHARS},
    {(char*)"snp",            required_argument, 0,            ARG_SNP},
    {(char*)"ss",             required_argument, 0,            ARG_SPLICESITE},
    {(char*)"sv",             required_argument, 0,            ARG_SV},
	{(char*)"help",           no_argument,       0,            'h'},
	{(char*)"ntoa",           no_argument,       0,            ARG_NTOA},
	{(char*)"justref",        no_argument,       0,            '3'},
	{(char*)"noref",          no_argument,       0,            'r'},
	{(char*)"sa",             no_argument,       0,            ARG_SA},
	{(char*)"reverse-each",   no_argument,       0,            ARG_REVERSE_EACH},
	{(char*)"usage",          no_argument,       0,            ARG_USAGE},
    {(char*)"wrapper",        required_argument, 0,            ARG_WRAPPER},
	{(char*)0, 0, 0, 0} // terminator
};

/**
 * Parse an int out of optarg and enforce that it be at least 'lower';
 * if it is less than 'lower', then output the given error message and
 * exit with an error and a usage message.
 */
template<typename T>
static int parseNumber(T lower, const char *errmsg) {
	char *endPtr= NULL;
	T t = (T)strtoll(optarg, &endPtr, 10);
	if (endPtr != NULL) {
		if (t < lower) {
			cerr << errmsg << endl;
			printUsage(cerr);
			throw 1;
		}
		return t;
	}
	cerr << errmsg << endl;
	printUsage(cerr);
	throw 1;
	return -1;
}

/**
 * Read command-line arguments
 */
static void parseOptions(int argc, const char **argv) {
	int option_index = 0;
	int next_option;
	do {
		next_option = getopt_long(
			argc, const_cast<char**>(argv),
			short_options, long_options, &option_index);
		switch (next_option) {
            case ARG_WRAPPER:
				wrapper = optarg;
				break;
			case 'f': format = FASTA; break;
			case 'c': format = CMDLINE; break;
			//case 'p': packed = true; break;
			case 'C':
				cerr << "Error: -C specified but Bowtie 2 does not support colorspace input." << endl;
				throw 1;
				break;
			case 'l':
				lineRate = parseNumber<int>(3, "-l/--lineRate arg must be at least 3");
				break;
			case 'i':
				linesPerSide = parseNumber<int>(1, "-i/--linesPerSide arg must be at least 1");
				break;
			case 'o':
				offRate = parseNumber<int>(0, "-o/--offRate arg must be at least 0");
				break;
            case ARG_LOCAL_OFFRATE:
                localOffRate = parseNumber<int>(0, "-o/--localoffrate arg must be at least 0");
                break;
			case '3':
				justRef = true;
				break;
			case 't':
				ftabChars = parseNumber<int>(1, "-t/--ftabChars arg must be at least 1");
				break;
            case ARG_LOCAL_FTABCHARS:
				localFtabChars = parseNumber<int>(1, "-t/--localftabchars arg must be at least 1");
				break;
			case 'n':
				// all f-s is used to mean "not set", so put 'e' on end
				bmax = 0xfffffffe;
				break;
			case 'h':
			case ARG_USAGE:
				printUsage(cout);
				throw 0;
				break;
            case ARG_SNP:
                snp_fname = optarg;
                break;
            case ARG_SPLICESITE:
                ss_fname = optarg;
                break;
            case ARG_SV:
                sv_fname = optarg;
                break;
			case ARG_BMAX:
				bmax = parseNumber<TIndexOffU>(1, "--bmax arg must be at least 1");
				bmaxMultSqrt = OFF_MASK; // don't use multSqrt
				bmaxDivN = 0xffffffff;     // don't use multSqrt
				break;
			case ARG_BMAX_MULT:
				bmaxMultSqrt = parseNumber<TIndexOffU>(1, "--bmaxmultsqrt arg must be at least 1");
				bmax = OFF_MASK;     // don't use bmax
				bmaxDivN = 0xffffffff; // don't use multSqrt
				break;
			case ARG_BMAX_DIV:
				bmaxDivN = parseNumber<uint32_t>(1, "--bmaxdivn arg must be at least 1");
				bmax = OFF_MASK;         // don't use bmax
				bmaxMultSqrt = OFF_MASK; // don't use multSqrt
				break;
			case ARG_DCV:
				dcv = parseNumber<int>(3, "--dcv arg must be at least 3");
				break;
			case ARG_SEED:
				seed = parseNumber<int>(0, "--seed arg must be at least 0");
				break;
			case ARG_REVERSE_EACH:
				reverseEach = true;
				break;
			case ARG_NTOA: nsToAs = true; break;
			case 'a': autoMem = false; break;
			case 'q': verbose = false; break;
			case 's': sanityCheck = true; break;
			case 'r': writeRef = false; break;
            case 'p':
                nthreads = parseNumber<int>(1, "-p arg must be at least 1");
                break;

			case -1: /* Done with options. */
				break;
			case 0:
				if (long_options[option_index].flag != 0)
					break;
			default:
				printUsage(cerr);
				throw 1;
		}
	} while(next_option != -1);
	if(bmax < 40) {
		cerr << "Warning: specified bmax is very small (" << bmax << ").  This can lead to" << endl
		     << "extremely slow performance and memory exhaustion.  Perhaps you meant to specify" << endl
		     << "a small --bmaxdivn?" << endl;
	}
}

EList<string> filesWritten;

/**
 * Delete all the index files that we tried to create.  For when we had to
 * abort the index-building process due to an error.
 */
static void deleteIdxFiles(
	const string& outfile,
	bool doRef,
	bool justRef)
{
	
	for(size_t i = 0; i < filesWritten.size(); i++) {
		cerr << "Deleting \"" << filesWritten[i].c_str()
		     << "\" file written during aborted indexing attempt." << endl;
		remove(filesWritten[i].c_str());
	}
}

extern void initializeCntLut();
extern void initializeCntBit();

/**
 * Drive the index construction process and optionally sanity-check the
 * result.
 */
template<typename TStr>
static void driver(
	const string& infile,
	EList<string>& infiles,
    const string& snpfile,
    const string& ssfile,
    const string& svfile,
	const string& outfile,
	bool packed,
	int reverse)
{
    initializeCntLut();
    initializeCntBit();
	EList<FileBuf*> is(MISC_CAT);
	bool bisulfite = false;
	RefReadInParams refparams(false, reverse, nsToAs, bisulfite);
	assert_gt(infiles.size(), 0);
	if(format == CMDLINE) {
		// Adapt sequence strings to stringstreams open for input
		stringstream *ss = new stringstream();
		for(size_t i = 0; i < infiles.size(); i++) {
			(*ss) << ">" << i << endl << infiles[i].c_str() << endl;
		}
		FileBuf *fb = new FileBuf(ss);
		assert(fb != NULL);
		assert(!fb->eof());
		assert(fb->get() == '>');
		ASSERT_ONLY(fb->reset());
		assert(!fb->eof());
		is.push_back(fb);
	} else {
		// Adapt sequence files to ifstreams
		for(size_t i = 0; i < infiles.size(); i++) {
			FILE *f = fopen(infiles[i].c_str(), "r");
			if (f == NULL) {
				cerr << "Error: could not open "<< infiles[i].c_str() << endl;
				throw 1;
			}
			FileBuf *fb = new FileBuf(f);
			assert(fb != NULL);
			if(fb->peek() == -1 || fb->eof()) {
				cerr << "Warning: Empty fasta file: '" << infile.c_str() << "'" << endl;
				continue;
			}
			assert(!fb->eof());
			assert(fb->get() == '>');
			ASSERT_ONLY(fb->reset());
			assert(!fb->eof());
			is.push_back(fb);
		}
	}
	if(is.empty()) {
		cerr << "Warning: All fasta inputs were empty" << endl;
		throw 1;
	}
	// Vector for the ordered list of "records" comprising the input
	// sequences.  A record represents a stretch of unambiguous
	// characters in one of the input sequences.
	EList<RefRecord> szs(MISC_CAT);
	std::pair<size_t, size_t> sztot;
	{
		if(verbose) cerr << "Reading reference sizes" << endl;
		Timer _t(cerr, "  Time reading reference sizes: ", verbose);
		if(!reverse && (writeRef || justRef)) {
			filesWritten.push_back(outfile + ".3." + gfm_ext);
			filesWritten.push_back(outfile + ".4." + gfm_ext);
			sztot = BitPairReference::szsFromFasta(is, outfile, bigEndian, refparams, szs, sanityCheck);
		} else {
			sztot = BitPairReference::szsFromFasta(is, string(), bigEndian, refparams, szs, sanityCheck);
		}
	}
	if(justRef) return;
	assert_gt(sztot.first, 0);
	assert_gt(sztot.second, 0);
	assert_gt(szs.size(), 0);
    
	// Construct index from input strings and parameters
	filesWritten.push_back(outfile + ".1." + gfm_ext);
	filesWritten.push_back(outfile + ".2." + gfm_ext);
	TStr s;
	HGFM<TIndexOffU> hGFM(
                          s,
                          packed,
                          1,  // TODO: maybe not?
                          lineRate,
                          offRate,      // suffix-array sampling rate
                          ftabChars,    // number of chars in initial arrow-pair calc
                          localOffRate,
                          localFtabChars,
                          nthreads,
                          snpfile,
                          ssfile,
                          svfile,
                          outfile,      // basename for .?.ht2 files
                          reverse == 0, // fw
                          !entireSA,    // useBlockwise
                          bmax,         // block size for blockwise SA builder
                          bmaxMultSqrt, // block size as multiplier of sqrt(len)
                          bmaxDivN,     // block size as divisor of len
                          noDc? 0 : dcv,// difference-cover period
                          is,           // list of input streams
                          szs,          // list of reference sizes
                          (TIndexOffU)sztot.first,  // total size of all unambiguous ref chars
                          refparams,    // reference read-in parameters
                          seed,         // pseudo-random number generator seed
                          -1,           // override offRate
                          verbose,      // be talkative
                          autoMem,      // pass exceptions up to the toplevel so that we can adjust memory settings automatically
                          sanityCheck); // verify results and internal consistency
    // Note that the Ebwt is *not* resident in memory at this time.  To
    // load it into memory, call ebwt.loadIntoMemory()
	if(verbose) {
		// Print Ebwt's vital stats
		hGFM.gh().print(cerr);
	}
	if(sanityCheck) {
		// Try restoring the original string (if there were
		// multiple texts, what we'll get back is the joined,
		// padded string, not a list)
		hGFM.loadIntoMemory(
                            reverse ? (refparams.reverse == REF_READ_REVERSE) : 0,
                            true,  // load SA sample?
                            true,  // load ftab?
                            true,  // load rstarts?
                            false,
                            false);
		SString<char> s2;
		hGFM.restore(s2);
		hGFM.evictFromMemory();
		{
			SString<char> joinedss = GFM<>::join<SString<char> >(
				is,          // list of input streams
				szs,         // list of reference sizes
				(TIndexOffU)sztot.first, // total size of all unambiguous ref chars
				refparams,   // reference read-in parameters
				seed);       // pseudo-random number generator seed
			if(refparams.reverse == REF_READ_REVERSE) {
				joinedss.reverse();
			}
			assert_eq(joinedss.length(), s2.length());
			assert(sstr_eq(joinedss, s2));
		}
		if(verbose) {
			if(s2.length() < 1000) {
				cout << "Passed restore check: " << s2.toZBuf() << endl;
			} else {
				cout << "Passed restore check: (" << s2.length() << " chars)" << endl;
			}
		}
	}
}

static const char *argv0 = NULL;

#if 0

const size_t SHIFT = 4;
const size_t BLOCKS = 1 << SHIFT;
const size_t BLOCK_MASK = BLOCKS - 1;

inline size_t radix(size_t num, size_t shift) {
    return (num >> shift) & BLOCK_MASK;
}

ELList<size_t> counts, indexes, indexes2;

inline void binSort(size_t* elts, size_t size, size_t depth = 0, size_t right_shift = 0) {
    if(depth == 0) {
        size_t log_size = sizeof(size_t) * 8;
        while(!(((size_t)1 << log_size) & size)) log_size--;
        right_shift = log_size + 1 - SHIFT;
    }
    
    if(right_shift <= 10) {
        sort(elts, elts + size);
        return;
    }

    size_t occupied = BLOCKS + 1;
    if(counts.size() <= depth) {
        counts.expand();
        indexes.expand();
        indexes2.expand();
    }
    EList<size_t>& count = counts[depth]; count.resizeExact(occupied); count.fillZero();
    for(size_t i = 0; i < size; i++) {
        size_t r = radix(elts[i], right_shift);
        count[r]++;
    }
    EList<size_t>& index = indexes[depth]; index.resizeExact(occupied); index.fillZero();
    for(size_t i = 1; i < occupied; i++) {
        index[i] = index[i - 1] + count[i - 1];
    }
    EList<size_t>& index2 = indexes2[depth]; index2 = index;
    for(size_t i = 0; i < occupied - 1; i++) {
        while(index2[i] < index[i+1]) {
            size_t elt = elts[index2[i]];
            size_t r = radix(elt, right_shift);
            while(true) {
                if(r == i) {
                    elts[index2[i]] = elt;
                    index2[i]++;
                    break;
                }
                size_t new_elt = elts[index2[r]];
                elts[index2[r]] = elt;
                index2[r]++;
                elt = new_elt;
                r = radix(elt, right_shift);
            }
        }
    }
    for(size_t i = 0; i < occupied - 1; i++) {
        binSort(elts + index[i], count[i], depth + 1, right_shift - SHIFT);
    }
}

inline void binSort2(size_t* elts, size_t size) {
    size_t log_size = sizeof(size_t) * 8;
    while(!(((size_t)1 << log_size) & size)) log_size--;
    size_t right_shift = log_size + 1 - SHIFT;
    
    EList<size_t*> elts_list; elts_list.resizeExact(16); elts_list.clear();
    EList<size_t> blocks; blocks.resizeExact(16); blocks.clear();
    ELList<size_t> counts, indexes, indexes2;
    
    size_t cur_depth = 0;
    while(true) {
        size_t* cur_elts = NULL;
        size_t cur_block = 0, cur_size = 0;
        int cur_shift = 0;
        if(cur_depth == 0) {
            cur_elts = elts;
            cur_block = 0;
            cur_size = size;
            cur_shift = right_shift;
        } else {
            cur_elts = elts_list.back();
            cur_block = blocks.back();
            cur_size = counts[cur_depth - 1][cur_block];
            cur_shift = (int)right_shift - (int)SHIFT * cur_depth;
        }
        if(cur_shift <= SHIFT) {
            sort(cur_elts, cur_elts + cur_size);
            elts_list.back() += cur_size;
            blocks.back() += 1;
            while(blocks.back() == BLOCKS || counts[cur_depth - 1][blocks.back()] == 0) {
                elts_list.pop_back();
                blocks.pop_back();
                cur_depth -= 1;
                if(cur_depth == 0) break;
                elts_list.back() += counts[cur_depth - 1][blocks.back()];
                blocks.back() += 1;
            }
            if(cur_depth == 0) break;
            continue;
        }
        if(counts.size() <= cur_depth) {
            counts.expand();
            indexes.expand();
            indexes2.expand();
        }
        size_t occupied = BLOCKS + 1;
        counts[cur_depth].resizeExact(occupied); counts[cur_depth].fillZero();
        size_t* count = counts[cur_depth].begin();
        for(size_t i = 0; i < cur_size; i++) {
            size_t r = radix(cur_elts[i], cur_shift);
            count[r]++;
        }
        indexes[cur_depth].resizeExact(occupied); indexes[cur_depth].fillZero();
        size_t* index = indexes[cur_depth].begin();
        for(size_t i = 1; i < occupied; i++) {
            index[i] = index[i - 1] + count[i - 1];
        }
        indexes2[cur_depth] = indexes[cur_depth];
        size_t* index2 = indexes2[cur_depth].begin();
        for(size_t i = 0; i < occupied - 1; i++) {
            size_t next = index[i+1];
            while(index2[i] < next) {
                size_t elt = cur_elts[index2[i]];
                size_t r = radix(elt, cur_shift);
                while(r != i) {
                    size_t new_elt = cur_elts[index2[r]];
                    cur_elts[index2[r]] = elt;
                    index2[r]++;
                    elt = new_elt;
                    r = radix(elt, cur_shift);
                }
                cur_elts[index2[i]] = elt;
                index2[i]++;
            }
        }
        cur_depth++;
        elts_list.push_back(cur_elts);
        blocks.push_back(0);
        assert_eq(cur_depth, elts_list.size());
        assert_eq(cur_depth, blocks.size());
    }
}

#endif

extern "C" {
/**
 * main function.  Parses command-line arguments.
 */
int hisat2_build(int argc, const char **argv) {
    
    // daehwan - for debugging purposes
#if 0
    size_t num_elts = (uint64_t)6 << 30;
    time_t prev = time(0);
    EList<size_t> elts; elts.resizeExact(num_elts); elts.fillZero();
    cout << "Num elts: " << elts.size() << "\t" << (clock() - prev) / (CLOCKS_PER_SEC / 1000) << " ms" << endl;
    prev = clock(); elts.fillZero();
    cout << "Num elts: " << elts.size() << "\t" << (clock() - prev) / (CLOCKS_PER_SEC / 1000) << " ms" << endl;
    
    prev = clock();
    for(size_t i = 0; i < elts.size(); i++) {
        elts[i] = i;
    }
    for(size_t i = 0; i < elts.size(); i++) {
        size_t r = ((size_t)rand() << 32 | (size_t)rand()) % elts.size();
        size_t t = elts[i];
        elts[i] = elts[r];
        elts[r] = t;
    }
    cout << "Random number generation: " << (clock() - prev) / (CLOCKS_PER_SEC / 1000) << " ms" << endl;

    const size_t rep = 1;
    prev = clock();
    for(size_t r = 0; r < rep; r++) {
        for(size_t i = 0; i < elts.size(); i++) {
            size_t tmp = elts[i];
            if(argc == 0) tmp += 1;
            elts[i] = tmp;
        }
    }
    cout << "Sequential memory access: " << (clock() - prev) / (CLOCKS_PER_SEC / 1000) << " ms" << endl;

    #if 0
    prev = clock();
    for(size_t r = 0; r < rep; r++) {
        size_t next = 0;
        for(size_t i = 0; i < elts.size(); i++) {
            size_t tmp = elts[next];
            if(argc == 0) tmp += 1;
            elts[next] = tmp;
            next = elts[next];
        }
    }
    cout << "Random memory access: " << (clock() - prev) / (CLOCKS_PER_SEC / 1000) << " ms" << endl;
    #endif
    
    prev = clock();
#if 1
    binSort2(elts.begin(), elts.size());
    cout << "Bin ";
#else
    cout << "Standard: ";
    elts.sort();
#endif
    cout << "Sorting: " << (clock() - prev) / (CLOCKS_PER_SEC / 1000) << " ms" << endl;
    
    for(size_t i = 0; i < elts.size(); i++) {
        if(elts[i] != i) {
            cout << "Not sorted: " << i << "\t" << elts[i] << endl;
            exit(1);
        }
    }
    cout << "Sorted" << endl;
    
    exit(1);
#endif
    
    string outfile;
	try {
		// Reset all global state, including getopt state
		opterr = optind = 1;
		resetOptions();

		string infile;
		EList<string> infiles(MISC_CAT);

		parseOptions(argc, argv);
		argv0 = argv[0];
		if(showVersion) {
			cout << argv0 << " version " << string(HISAT2_VERSION).c_str() << endl;
			if(sizeof(void*) == 4) {
				cout << "32-bit" << endl;
			} else if(sizeof(void*) == 8) {
				cout << "64-bit" << endl;
			} else {
				cout << "Neither 32- nor 64-bit: sizeof(void*) = " << sizeof(void*) << endl;
			}
			cout << "Built on " << BUILD_HOST << endl;
			cout << BUILD_TIME << endl;
			cout << "Compiler: " << COMPILER_VERSION << endl;
			cout << "Options: " << COMPILER_OPTIONS << endl;
			cout << "Sizeof {int, long, long long, void*, size_t, off_t}: {"
				 << sizeof(int)
				 << ", " << sizeof(long) << ", " << sizeof(long long)
				 << ", " << sizeof(void *) << ", " << sizeof(size_t)
				 << ", " << sizeof(off_t) << "}" << endl;
			return 0;
		}

		// Get input filename
		if(optind >= argc) {
			cerr << "No input sequence or sequence file specified!" << endl;
			printUsage(cerr);
			return 1;
		}
		infile = argv[optind++];
        
		// Get output filename
		if(optind >= argc) {
			cerr << "No output file specified!" << endl;
			printUsage(cerr);
			return 1;
		}
		outfile = argv[optind++];

		tokenize(infile, ",", infiles);
		if(infiles.size() < 1) {
			cerr << "Tokenized input file list was empty!" << endl;
			printUsage(cerr);
			return 1;
		}

		// Optionally summarize
		if(verbose) {
			cerr << "Settings:" << endl
				 << "  Output files: \"" << outfile.c_str() << ".*." << gfm_ext << "\"" << endl
				 << "  Line rate: " << lineRate << " (line is " << (1<<lineRate) << " bytes)" << endl
				 << "  Lines per side: " << linesPerSide << " (side is " << ((1<<lineRate)*linesPerSide) << " bytes)" << endl
				 << "  Offset rate: " << offRate << " (one in " << (1<<offRate) << ")" << endl
				 << "  FTable chars: " << ftabChars << endl
				 << "  Strings: " << (packed? "packed" : "unpacked") << endl
                 << "  Local offset rate: " << localOffRate << " (one in " << (1<<localOffRate) << ")" << endl
                 << "  Local fTable chars: " << localFtabChars << endl
                 << "  Local sequence length: " << local_index_size << endl
                 << "  Local sequence overlap between two consecutive indexes: " << local_index_overlap << endl;
#if 0
			if(bmax == OFF_MASK) {
				cerr << "  Max bucket size: default" << endl;
			} else {
				cerr << "  Max bucket size: " << bmax << endl;
			}
			if(bmaxMultSqrt == OFF_MASK) {
				cerr << "  Max bucket size, sqrt multiplier: default" << endl;
			} else {
				cerr << "  Max bucket size, sqrt multiplier: " << bmaxMultSqrt << endl;
			}
			if(bmaxDivN == 0xffffffff) {
				cerr << "  Max bucket size, len divisor: default" << endl;
			} else {
				cerr << "  Max bucket size, len divisor: " << bmaxDivN << endl;
			}
			cerr << "  Difference-cover sample period: " << dcv << endl;
#endif
			cerr << "  Endianness: " << (bigEndian? "big":"little") << endl
				 << "  Actual local endianness: " << (currentlyBigEndian()? "big":"little") << endl
				 << "  Sanity checking: " << (sanityCheck? "enabled":"disabled") << endl;
	#ifdef NDEBUG
			cerr << "  Assertions: disabled" << endl;
	#else
			cerr << "  Assertions: enabled" << endl;
	#endif
			cerr << "  Random seed: " << seed << endl;
			cerr << "  Sizeofs: void*:" << sizeof(void*) << ", int:" << sizeof(int) << ", long:" << sizeof(long) << ", size_t:" << sizeof(size_t) << endl;
			cerr << "Input files DNA, " << file_format_names[format].c_str() << ":" << endl;
			for(size_t i = 0; i < infiles.size(); i++) {
				cerr << "  " << infiles[i].c_str() << endl;
			}
		}
		// Seed random number generator
		srand(seed);
		{
			Timer timer(cerr, "Total time for call to driver() for forward index: ", verbose);
            try {
                driver<SString<char> >(infile, infiles, snp_fname, ss_fname, sv_fname, outfile, false, REF_READ_FORWARD);
            } catch(bad_alloc& e) {
                if(autoMem) {
                    cerr << "Switching to a packed string representation." << endl;
                    packed = true;
                } else {
                    throw e;
                }
            }
		}
		return 0;
	} catch(std::exception& e) {
		cerr << "Error: Encountered exception: '" << e.what() << "'" << endl;
		cerr << "Command: ";
		for(int i = 0; i < argc; i++) cerr << argv[i] << " ";
		cerr << endl;
		deleteIdxFiles(outfile, writeRef || justRef, justRef);
		return 1;
	} catch(int e) {
		if(e != 0) {
			cerr << "Error: Encountered internal HISAT2 exception (#" << e << ")" << endl;
			cerr << "Command: ";
			for(int i = 0; i < argc; i++) cerr << argv[i] << " ";
			cerr << endl;
		}
		deleteIdxFiles(outfile, writeRef || justRef, justRef);
		return e;
	}
}
}
