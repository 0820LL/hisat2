/*
 * Copyright 2015, Daehwan Kim <infphilo@gmail.com>, Joe Paggi <jpaggi@mit.edu>
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

#ifndef GBWT_GRAPH_H_
#define GBWT_GRAPH_H_

#include <string>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <stdlib.h>
#include <map>
#include <deque>
#include "snp.h"
#include <time.h>
#include "radix_sort.h"

// Reference:
// Jouni Sirén, Niko Välimäki, and Veli Mäkinen: Indexing Graphs for Path Queries with Applications in Genome Research.
// IEEE/ACM Transactions on Computational Biology and Bioinformatics 11(2):375-388, 2014.
// http://ieeexplore.ieee.org/xpl/articleDetails.jsp?arnumber=6698337

//--------------------------------------------------------------------------

template <typename index_t> class PathGraph;

// Note: I wrote the following codes based on Siren's work, gcsa (see the reference above).
template <typename index_t>
class RefGraph {
    friend class PathGraph<index_t>;
public:
    struct Node {
        char    label; // ACGTN + Y(head) + Z(tail)
        index_t value; // location in a whole genome
        
        Node() { reset(); }
        Node(char label_, index_t value_) : label(label_), value(value_) {}
        void reset() { label = 0; value = 0; }

        bool write(ofstream& f_out, bool bigEndian) const {
            writeIndex<index_t>(f_out, value, bigEndian);
            writeU16(f_out, label, bigEndian);
            return true;
        }

        bool read(ifstream& f_in, bool bigEndian) {
            value = readIndex<index_t>(f_in, bigEndian);
            label = (char)readU16(f_in, bigEndian);
            return true;
        }

        bool operator== (const Node& o) const {
            if(value != o.value) return false;
            if(label != o.label) return false;
            return true;
        }

        bool operator< (const Node& o) const {
            if(value != o.value) return value < o.value;
            return label < o.label;
        }
    };

    struct Edge {
        index_t from; // from Node
        index_t to;   // to Node

        Edge() {}
        Edge(index_t from_, index_t to_) : from(from_), to(to_) {}

        bool write(ofstream& f_out, bool bigEndian) const {
            writeIndex<index_t>(f_out, from, bigEndian);
            writeIndex<index_t>(f_out, to, bigEndian);
            return true;
        }

        bool read(ifstream& f_in, bool bigEndian) {
            from = readIndex<index_t>(f_in, bigEndian);
            to = readIndex<index_t>(f_in, bigEndian);
            return true;
        }

        bool operator< (const Edge& o) const {
            if(from != o.from) return from < o.from;
            return to < o.to;
        }
    };
    static index_t EdgeTo (Edge& a) {
    	return a.to;
    }

    struct EdgeFromCmp {
        bool operator() (const Edge& a, const Edge& b) const {
            return a.from < b.from;
        }
    };

    struct EdgeToCmp {
        bool operator() (const Edge& a, const Edge& b) const {
            return a.to < b.to;
        };
    };

public:
    RefGraph(const SString<char>& s,
             const EList<RefRecord>& szs,
             const EList<SNP<index_t> >& snps,
             const string& out_fname,
             int nthreads_,
             bool verbose);

    bool repOk() { return true; }

    void write(const std::string& base_name) {}
    void printInfo() {}

private:
    static bool isReverseDeterministic(EList<Node>& nodes, EList<Edge>& edges);
    static void reverseDeterminize(EList<Node>& nodes, EList<Edge>& edges, index_t& lastNode, index_t lastNode_add = 0);

    static void sortEdgesFrom(EList<Edge>& edges) {
        std::sort(edges.begin(), edges.end(), EdgeFromCmp());
    }
    static void sortEdgesTo(EList<Edge>& edges) {
    	bin_sort_no_copy<Edge, EdgeToCmp, index_t>(
    	    				edges.begin(), edges.end(), &EdgeTo, (index_t)-1);
    }
    void sortEdgesByTo() {
    	bin_sort_no_copy<Edge, EdgeToCmp, index_t>(
    				edges.begin(), edges.end(), &EdgeTo, edges.size(), nthreads);
    }

    // Return edge ranges [begin, end)
    static pair<index_t, index_t> findEdges(const EList<Edge>& edges, index_t node, bool from);
    static pair<index_t, index_t> findEdgesFrom(const EList<Edge>& edges, index_t node) {
        return findEdges(edges, node, true);
    }
    static pair<index_t, index_t> findEdgesTo(const EList<Edge>& edges, index_t node) {
        return findEdges(edges, node, false);
    }
    static pair<index_t, index_t> getNextEdgeRange(const EList<Edge>& sep_edges, pair<index_t, index_t> range, bool from) {
        if(range.second >= sep_edges.size()) {
            return pair<index_t, index_t>(0, 0);
        }
        range.first = range.second; range.second++;

        if(from) {
            while(range.second < sep_edges.size() && sep_edges[range.second].from == sep_edges[range.first].from) {
                range.second++;
            }
        } else {
            while(range.second < sep_edges.size() && sep_edges[range.second].to == sep_edges[range.first].to) {
                range.second++;
            }
        }
        return range;
    }

private:
    struct ThreadParam {
        // input
        index_t                      thread_id;
        RefGraph<index_t>*           refGraph;
        const SString<char>*         s;
        const EList<SNP<index_t> >*  snps;
        string                       out_fname;
        bool                         bigEndian;

        // output
        index_t                      num_nodes;
        index_t                      num_edges;
        index_t                      lastNode;
        bool                         multipleHeadNodes;
    };
    static void buildGraph_worker(void* vp);

private:
    EList<RefRecord> szs;
    EList<RefRecord> tmp_szs;

    EList<Node> nodes;
    EList<Edge> edges;
    index_t     lastNode; // Z

    int         nthreads;

#ifndef NDEBUG
    bool        debug;
#endif

private:
    // Following composite nodes and edges are used to reverse-determinize an automaton.
    struct CompositeNodeIDs {
        index_t id;
        EList<index_t> add_ids;
        
        CompositeNodeIDs() {
            id = (index_t)INDEX_MAX;
        }      
        
        bool operator<(const CompositeNodeIDs& o) const {
            if(id != o.id) return id < o.id;
            if(add_ids.size() != o.add_ids.size()) return add_ids.size() < o.add_ids.size();
            for(index_t i = 0; i < add_ids.size(); i++) {
                assert_lt(i, o.add_ids.size());
                if(add_ids[i] != o.add_ids[i]) return add_ids[i] < o.add_ids[i];
            }
            return false;
        }
        
        index_t size() const {
            if(id == (index_t)INDEX_MAX) return 0;
            return add_ids.size() + 1;
        }
        index_t getID(index_t i) const {
            if(i == 0) return id;
            else {
                i -= 1;
                assert_lt(i, add_ids.size());
                return add_ids[i];
            }
        }
        void push_back(index_t node_id) {
            if(id == (index_t)INDEX_MAX) id = node_id;
            else add_ids.push_back(node_id);
        }
    };
    
    struct CompositeNode {
        CompositeNodeIDs           nodes;
        index_t                    id;
        char                       label;
        index_t                    value;

        CompositeNode() { reset(); }

        CompositeNode(char label_, index_t node_id) :
        id(0), label(label_)
        {
            nodes.push_back(node_id);
        }

        Node getNode() const {
            return Node(label, value);
        }

        void reset() {
            nodes.id = (index_t)INDEX_MAX;
            nodes.add_ids.clear();
            id = 0;
            label = 0;
            value = 0;
        }
    };

    struct CompositeEdge {
        index_t from;
        index_t to;

        CompositeEdge() : from(0), to(0) {}
        CompositeEdge(index_t from_, index_t to_) : from(from_), to(to_) {}

        Edge getEdge(const EList<CompositeNode>& nodes) const
        {
            assert_lt(from, nodes.size());
            const CompositeNode& from_node = nodes[from];
            assert_lt(to, nodes.size());
            const CompositeNode& to_node = nodes[to];
            return Edge(from_node.id, to_node.id);
        }

        bool operator < (const CompositeEdge& o) const
        {
            return from < o.from;
        }
    };

    struct TempNodeLabelCmp {
        TempNodeLabelCmp(const EList<Node>& nodes_) : nodes(nodes_) {}
        bool operator() (index_t a, index_t b) const {
            assert_lt(a, nodes.size());
            assert_lt(b, nodes.size());
            return nodes[a].label < nodes[b].label;
        }

        const EList<Node>& nodes;
    };
};

/**
 * Load reference sequence file and snp information.
 * Construct a reference graph
 */
template <typename index_t>
RefGraph<index_t>::RefGraph(const SString<char>& s,
                            const EList<RefRecord>& szs,
                            const EList<SNP<index_t> >& snps,
                            const string& out_fname,
                            int nthreads_,
                            bool verbose)
: lastNode(0), nthreads(nthreads_)
{
    const bool bigEndian = false;

    assert_gt(nthreads, 0);
    assert_gt(szs.size(), 0);
    index_t jlen = s.length();

#ifndef NDEBUG
    debug = (jlen <= 20);
#endif

    // a memory-efficient way to create a population graph with known SNPs
    bool frag_automaton = jlen >= (1 << 16);
    if(frag_automaton) {
        {
            EList<pair<index_t, index_t> > snp_ranges; // each range inclusive
            for(index_t i = 0; i < snps.size(); i++) {
                const SNP<index_t>& snp = snps[i];
                index_t left_relax = 10, right_relax = 10;
                if(snp.type == SNP_INS) {
                    right_relax = 128;
                }
                pair<index_t, index_t> range;
                range.first = snp.pos > left_relax ? snp.pos - left_relax - 1 : 0;
                if(snp.type == SNP_SGL) {
                    range.second = snp.pos + 1;
                } else if(snp.type == SNP_DEL) {
                    assert_gt(snp.len, 0);
                    range.second = snp.pos + snp.len;
                } else if(snp.type == SNP_INS) {
                    assert_gt(snp.len, 0);
                    range.second = snp.pos;
                } else assert(false);
                range.second += right_relax;

                if(snp_ranges.empty() || snp_ranges.back().second + 1 < range.first) {
                    snp_ranges.push_back(range);
                } else {
                    assert_leq(snp_ranges.back().first, range.first);
                    if(snp_ranges.back().second < range.second) {
                        snp_ranges.back().second = range.second;
                    }
                }
            }

            index_t chunk_size = 1 << 20;
            index_t pos = 0, range_idx = 0;
            for(index_t i = 0; i < szs.size(); i++) {
                if(szs[i].len == 0) continue;
                if(szs[i].len <= chunk_size) {
                    tmp_szs.push_back(szs[i]);
                    pos += szs[i].len;
                } else {
                    index_t num_chunks = (szs[i].len + chunk_size - 1) / chunk_size;
                    assert_gt(num_chunks, 1);
                    index_t modified_chunk_size = szs[i].len / num_chunks;
                    index_t after_pos = pos + szs[i].len;
                    ASSERT_ONLY(index_t sum_len = 0);
                    while(pos < after_pos) {
                        index_t target_pos = pos + modified_chunk_size;
                        if(target_pos < after_pos) {
                            for(; range_idx < snp_ranges.size(); range_idx++) {
                                if(target_pos < snp_ranges[range_idx].first) break;
                            }
                            pair<index_t, index_t> snp_free_range;
                            if(range_idx == 0) {
                                snp_free_range.first = 0;
                            } else {
                                snp_free_range.first = snp_ranges[range_idx - 1].second + 1;
                                if(snp_free_range.first >= jlen) {
                                    snp_free_range.first = jlen - 1;
                                }
                            }

                            if(range_idx == snp_ranges.size()) {
                                snp_free_range.second = jlen - 1;
                            } else {
                                snp_free_range.second = snp_ranges[range_idx].first - 1;
                            }

                            assert_leq(snp_free_range.first, snp_free_range.second);
                            if(target_pos < snp_free_range.first) target_pos = snp_free_range.first;
                            if(target_pos > after_pos) target_pos = after_pos;
                        } else {
                            target_pos = after_pos;
                        }

                        tmp_szs.expand();
                        tmp_szs.back().len = target_pos - pos;
                        tmp_szs.back().off = 0;
                        pos = target_pos;
                        ASSERT_ONLY(sum_len += tmp_szs.back().len);
                    }
                    assert_eq(pos, after_pos);
                    assert_eq(sum_len, szs[i].len);
                }
            }
#ifndef NDEBUG
            index_t modified_jlen = 0;
            for(index_t i = 0; i < tmp_szs.size(); i++) {
                modified_jlen += tmp_szs[i].len;
            }
            assert_eq(modified_jlen, jlen);
#endif
        }

        assert_gt(nthreads, 0);
        AutoArray<tthread::thread*> threads(nthreads);
        EList<ThreadParam> threadParams;
        for(index_t i = 0; i < (index_t)nthreads; i++) {
            threadParams.expand();
            threadParams.back().thread_id = i;
            threadParams.back().refGraph = this;
            threadParams.back().s = &s;
            threadParams.back().snps = &snps;
            threadParams.back().out_fname = out_fname;
            threadParams.back().bigEndian = bigEndian;
            threadParams.back().num_nodes = 0;
            threadParams.back().num_edges = 0;
            threadParams.back().lastNode = 0;
            threadParams.back().multipleHeadNodes = false;
            if(nthreads == 1) {
                buildGraph_worker((void*)&threadParams.back());
            } else {
                threads[i] = new tthread::thread(buildGraph_worker, (void*)&threadParams.back());
            }
        }

        if(nthreads > 1) {
            for(index_t i = 0; i < (index_t)nthreads; i++)
                threads[i]->join();
        }

        index_t num_nodes = 0, num_edges = 0;
        for(index_t i = 0; i < threadParams.size(); i++) {
            num_nodes += threadParams[i].num_nodes;
            num_edges += threadParams[i].num_edges;
            // Make room for edges spanning graphs by different threads
            if(i > 0) {
                num_edges += 16;
            }
        }
        nodes.resizeExact(num_nodes); nodes.clear();
        edges.resizeExact(num_edges); edges.clear();

        // Read all the nodes and edges
        EList<index_t> tail_nodes;
        bool multipleHeadNodes = false;
        for(index_t i = 0; i < threadParams.size(); i++) {
            if(threadParams[i].multipleHeadNodes) multipleHeadNodes = true;
            std::ostringstream number; number << i;
            string rg_fname = out_fname + "." + number.str() + ".rf";
            ifstream rg_in_file(rg_fname.c_str(), ios::binary);
            if(!rg_in_file.good()) {
                cerr << "Could not open file for reading a reference graph: \"" << rg_fname << "\"" << endl;
                throw 1;
            }
            index_t curr_num_nodes = nodes.size();
            ASSERT_ONLY(index_t curr_num_edges = edges.size());
            ASSERT_ONLY(index_t num_spanning_edges = 0);
            // Read nodes to be connected to last nodes in a previous thread
            if(i > 0) {
                assert_gt(tail_nodes.size(), 0)
                index_t num_head_nodes = readIndex<index_t>(rg_in_file, bigEndian);
                for(index_t j = 0; j < num_head_nodes; j++) {
                    index_t head_node = readIndex<index_t>(rg_in_file, bigEndian);
                    for(index_t k = 0; k < tail_nodes.size(); k++) {
                        edges.expand();
                        edges.back().from = tail_nodes[k];
                        edges.back().to = head_node + curr_num_nodes;
                        ASSERT_ONLY(num_spanning_edges++);
                    }
                }
            }
            while(!rg_in_file.eof()) {
                index_t tmp_num_nodes = readIndex<index_t>(rg_in_file, bigEndian);
                for(index_t i = 0; i < tmp_num_nodes; i++) {
                    nodes.expand();
                    nodes.back().read(rg_in_file, bigEndian);
                }
                index_t tmp_num_edges = readIndex<index_t>(rg_in_file, bigEndian);
                for(index_t i = 0; i < tmp_num_edges; i++) {
                    edges.expand();
                    edges.back().read(rg_in_file, bigEndian);
                    edges.back().from += curr_num_nodes;
                    edges.back().to += curr_num_nodes;
                }

                if(nodes.size() >= curr_num_nodes + threadParams[i].num_nodes) {
                    assert_eq(nodes.size(), curr_num_nodes + threadParams[i].num_nodes);
                    assert_eq(edges.size(), curr_num_edges + num_spanning_edges + threadParams[i].num_edges);
                    // Read last nodes in this thread
                    tail_nodes.clear();
                    if(i + 1 < (index_t)nthreads) {
                        index_t num_tail_nodes = readIndex<index_t>(rg_in_file, bigEndian);
                        for(index_t j = 0; j < num_tail_nodes; j++) {
                            index_t tail_node = readIndex<index_t>(rg_in_file, bigEndian);
                            tail_nodes.push_back(tail_node + curr_num_nodes);
                        }
                    }
                    break;
                }
            }
            rg_in_file.close();
            std::remove(rg_fname.c_str());
            if(i + 1 == (index_t)nthreads) {
                lastNode = threadParams.back().lastNode + curr_num_nodes;
                assert_lt(lastNode, nodes.size());
                assert_eq(nodes[lastNode].label, 'Z');
            }
        }

        if(multipleHeadNodes) {
            if(!isReverseDeterministic(nodes, edges)) {
                if(verbose) cerr << "\tis not reverse-deterministic, so reverse-determinize..." << endl;
                reverseDeterminize(nodes, edges, lastNode);
            }
        }
        assert(isReverseDeterministic(nodes, edges));
    } else { // this is memory-consuming, but simple to implement
        index_t num_predicted_nodes = (index_t)(jlen * 1.2);
        nodes.reserveExact(num_predicted_nodes);
        edges.reserveExact(num_predicted_nodes);

        // Created head node
        nodes.expand();
        nodes.back().label = 'Y';
        nodes.back().value = 0;
        // Create nodes and edges corresponding to a reference genome
        for(size_t i = 0; i < s.length(); i++) {
            nodes.expand();
            nodes.back().label = "ACGT"[(int)s[i]];
            nodes.back().value = i;

            assert_geq(nodes.size(), 2);
            edges.expand();
            edges.back().from = nodes.size() - 2;
            edges.back().to = nodes.size() - 1;
        }

        // Create tail node
        nodes.expand();
        nodes.back().label = 'Z';
        nodes.back().value = s.length();
        lastNode = nodes.size() - 1;
        edges.expand();
        edges.back().from = nodes.size() - 2;
        edges.back().to = nodes.size() - 1;

        // Create nodes and edges for SNPs
        for(size_t i = 0; i < snps.size(); i++) {
            const SNP<index_t>& snp = snps[i];
            if(snp.pos >= s.length()) break;
            if(snp.type == SNP_SGL) {
                assert_eq(snp.len, 1);
                nodes.expand();
                assert_lt(snp.seq, 4);
                assert_neq(snp.seq & 0x3, s[snp.pos]);
                nodes.back().label = "ACGT"[snp.seq];
                nodes.back().value = snp.pos;
                edges.expand();
                edges.back().from = snp.pos;
                edges.back().to = nodes.size() - 1;
                edges.expand();
                edges.back().from = nodes.size() - 1;
                edges.back().to = snp.pos + 2;
            }
            else if(snp.type == SNP_DEL) {
                assert_gt(snp.len, 0);
                if(snp.pos + snp.len >= s.length()) break;
                edges.expand();
                edges.back().from = snp.pos;
                edges.back().to = snp.pos + snp.len + 1;
            } else if(snp.type == SNP_INS) {
                assert_gt(snp.len, 0);
                for(size_t j = 0; j < snp.len; j++) {
                    uint64_t bp = snp.seq >> ((snp.len - j - 1) << 1);
                    bp &= 0x3;
                    char ch = "ACGT"[bp];
                    nodes.expand();
                    nodes.back().label = ch;
                    nodes.back().value = (index_t)INDEX_MAX;
                    edges.expand();
                    edges.back().from = (j == 0 ? snp.pos : nodes.size() - 2);
                    edges.back().to = nodes.size() - 1;
                }
                edges.expand();
                edges.back().from = nodes.size() - 1;
                edges.back().to = snp.pos + 1;
            } else {
                assert(false);
            }
        }

        if(!isReverseDeterministic(nodes, edges)) {
            if(verbose) cerr << "\tis not reverse-deterministic, so reverse-determinize..." << endl;
            reverseDeterminize(nodes, edges, lastNode);
            assert(isReverseDeterministic(nodes, edges));
        }
    }
    
    // daehwan - for debugging purposes
#if 0
    cout << "num nodes: " << nodes.size() << endl;
    for(index_t i = 0; i < nodes.size(); i++) {
        const Node& n = nodes[i];
        cout << i << "\t" << n.label << "\t" << n.value << endl;
    }
    
    sort(edges.begin(), edges.end());
    cout << "num edges: " << edges.size() << endl;
    for(index_t i = 0; i < edges.size(); i++) {
        const Edge& e = edges[i];
        cout << i << "\t" << e.from << " --> " << e.to << endl;
    }
    exit(1);
#endif
}

template <typename index_t>
pair<index_t, index_t> RefGraph<index_t>::findEdges(const EList<Edge>& edges, index_t node, bool from) {
    pair<index_t, index_t> range(0, 0);
    assert_gt(edges.size(), 0);

    // Find lower bound
    index_t low = 0, high = edges.size() - 1;
    index_t temp;
    while(low < high) {
        index_t mid = low + (high - low) / 2;
        temp = (from ? edges[mid].from : edges[mid].to);
        if(node == temp) {
            high = mid;
        } else if(node < temp) {
            if(mid == 0) {
                return pair<index_t, index_t>(0, 0);
            }

            high = mid - 1;
        } else {
            low = mid + 1;
        }
    }
    temp = (from ? edges[low].from : edges[low].to);
    if(node == temp) {
        range.first = low;
    } else {
        return range;
    }

    // Find upper bound
    high = edges.size() - 1;
    while(low < high)
    {
        index_t mid = low + (high - low + 1) / 2;
        temp = (from ? edges[mid].from : edges[mid].to);
        if(node == temp) {
            low = mid;
        } else {
            assert_lt(node, temp);
            high = mid - 1;
        }
    }
#ifndef NDEBUG
    temp = (from ? edges[high].from : edges[high].to);
    assert_eq(node, temp);
#endif
    range.second = high + 1;
    return range;
}

template <typename index_t>
void RefGraph<index_t>::buildGraph_worker(void* vp) {
    ThreadParam* threadParam = (ThreadParam*)vp;
    RefGraph<index_t>& refGraph = *(threadParam->refGraph);

    const SString<char>& s = *(threadParam->s);
    index_t jlen = s.length();

    const EList<SNP<index_t> >& snps = *(threadParam->snps);

    EList<Node> nodes; EList<Edge> edges;
    const EList<RefRecord>& tmp_szs = refGraph.tmp_szs;

    index_t thread_id = threadParam->thread_id;
    index_t nthreads = refGraph.nthreads;
    std::ostringstream number; number << thread_id;
    const string rg_fname = threadParam->out_fname + "." + number.str() + ".rf";
    ofstream rg_out_file(rg_fname.c_str(), ios::binary);
    if(!rg_out_file.good()) {
        cerr << "Could not open file for writing a reference graph: \"" << rg_fname << "\"" << endl;
        throw 1;
    }

    const bool bigEndian = threadParam->bigEndian;

    index_t& lastNode = threadParam->lastNode;

    index_t& num_nodes = threadParam->num_nodes;
    index_t& num_edges = threadParam->num_edges;
    index_t szs_idx = 0, szs_idx_end = tmp_szs.size();
    if(threadParam->thread_id != 0) {
        szs_idx = (tmp_szs.size() / nthreads) * thread_id;
    }
    if(thread_id + 1 < nthreads) {
        szs_idx_end = (tmp_szs.size() / nthreads) * (thread_id + 1);
    }

    index_t curr_pos = 0;
    for(index_t i = 0; i < szs_idx; i++) {
        curr_pos += tmp_szs[i].len;
    }
    EList<index_t> prev_tail_nodes;
    index_t snp_idx = 0;
    for(; szs_idx < szs_idx_end; szs_idx++) {
        index_t curr_len = tmp_szs[szs_idx].len;
        if(curr_len <= 0) continue;
        index_t num_predicted_nodes = (index_t)(curr_len * 1.2);
        nodes.resizeExact(num_predicted_nodes); nodes.clear();
        edges.resizeExact(num_predicted_nodes); edges.clear();

        // Created head node
        nodes.expand();
        nodes.back().label = 'Y';
        nodes.back().value = 0;

        // Create nodes and edges corresponding to a reference genome
        assert_leq(curr_pos + curr_len, s.length());
        for(size_t i = curr_pos; i < curr_pos + curr_len; i++) {
            nodes.expand();
            nodes.back().label = "ACGT"[(int)s[i]];
            nodes.back().value = i;
            assert_geq(nodes.size(), 2);
            edges.expand();
            edges.back().from = nodes.size() - 2;
            edges.back().to = nodes.size() - 1;
        }

        // Create tail node
        nodes.expand();
        nodes.back().label = 'Z';
        nodes.back().value = s.length();
        lastNode = nodes.size() - 1;
        edges.expand();
        edges.back().from = nodes.size() - 2;
        edges.back().to = nodes.size() - 1;

        // Create nodes and edges for SNPs
        for(; snp_idx < snps.size(); snp_idx++) {
            const SNP<index_t>& snp = snps[snp_idx];
            if(snp.pos < curr_pos) continue;
            assert_geq(snp.pos, curr_pos);
            if(snp.pos >= curr_pos + curr_len) break;
            if(snp.type == SNP_SGL) {
                assert_eq(snp.len, 1);
                nodes.expand();
                assert_lt(snp.seq, 4);
                assert_neq(snp.seq & 0x3, s[snp.pos]);
                nodes.back().label = "ACGT"[snp.seq];
                nodes.back().value = snp.pos;
                edges.expand();
                edges.back().from = snp.pos - curr_pos;
                edges.back().to = nodes.size() - 1;
                edges.expand();
                edges.back().from = nodes.size() - 1;
                edges.back().to = snp.pos - curr_pos + 2;
            } else if(snp.type == SNP_DEL) {
                assert_gt(snp.len, 0);
                edges.expand();
                edges.back().from = snp.pos - curr_pos;
                edges.back().to = snp.pos - curr_pos + snp.len + 1;
            } else if(snp.type == SNP_INS) {
                assert_gt(snp.len, 0);
                for(size_t j = 0; j < snp.len; j++) {
                    uint64_t bp = snp.seq >> ((snp.len - j - 1) << 1);
                    bp &= 0x3;
                    char ch = "ACGT"[bp];
                    nodes.expand();
                    nodes.back().label = ch;
                    nodes.back().value = (index_t)INDEX_MAX;
                    edges.expand();
                    edges.back().from = (j == 0 ? snp.pos - curr_pos : nodes.size() - 2);
                    edges.back().to = nodes.size() - 1;
                }
                edges.expand();
                edges.back().from = nodes.size() - 1;
                edges.back().to = snp.pos - curr_pos + 1;
            } else {
                assert(false);
            }
        }

#ifndef NDEBUG
        if(refGraph.debug) {
            cerr << "Nodes:" << endl;
            for(size_t i = 0; i < nodes.size(); i++) {
                const Node& node = nodes[i];
                cerr << "\t" << i << "\t" << node.label << "\t" << node.value << endl;
            }
            cerr << endl;
            cerr << "Edges: " << endl;
            for(size_t i = 0; i < edges.size(); i++) {
                const Edge& edge = edges[i];
                cerr << "\t" << i << "\t" << edge.from << " --> " << edge.to << endl;
            }
            cerr << endl;
        }
#endif

        if(!isReverseDeterministic(nodes, edges)) {
            reverseDeterminize(nodes, edges, lastNode, curr_pos > 0 ? curr_pos + 1 : 0);
            assert(isReverseDeterministic(nodes, edges));
        }

        // Identify head
        index_t head_node = nodes.size();
        for(index_t i = 0; i < nodes.size(); i++) {
            if(nodes[i].label == 'Y') {
                head_node = i;
                break;
            }
        }
        assert_lt(head_node, nodes.size());
        index_t tail_node = lastNode; assert_lt(tail_node, nodes.size());

        // Update edges
        const index_t invalid = (index_t)INDEX_MAX;
        bool head_off = curr_pos > 0, tail_off = curr_pos + curr_len < jlen;
        for(index_t i = 0; i < edges.size(); i++) {
            index_t from = edges[i].from;
            from = from + num_nodes;
            if(head_off && edges[i].from > head_node) from -= 1;
            if(tail_off && edges[i].from > tail_node) from -= 1;
            if(head_off && edges[i].from == head_node) {
                edges[i].from = invalid;
            } else {
                edges[i].from = from;
            }

            index_t to = edges[i].to;
            to = to + num_nodes;
            if(head_off && edges[i].to > head_node) to -= 1;
            if(tail_off && edges[i].to > tail_node) to -= 1;
            if(tail_off && edges[i].to == tail_node) {
                edges[i].to = invalid;
            } else {
                edges[i].to = to;
            }
        }
        head_node = tail_node = invalid;
        // Also update lastNode
        if(!tail_off) {
            lastNode += num_nodes;
            if(head_off) lastNode -= 1;
        }

        // Connect head nodes with tail nodes in the previous automaton
        index_t num_head_nodes = 0;
        index_t tmp_num_edges = edges.size();
        if(head_off) {
            EList<index_t> nodes_to_head;
            for(index_t i = 0; i < tmp_num_edges; i++) {
                if(edges[i].from == head_node) {
                    num_head_nodes++;
                    if(prev_tail_nodes.size() > 0) {
                        for(index_t j = 0; j < prev_tail_nodes.size(); j++) {
                            edges.expand();
                            edges.back().from = prev_tail_nodes[j];
                            edges.back().to = edges[i].to;
                            assert_lt(edges.back().from, edges.back().to);
                        }
                    } else {
                        nodes_to_head.push_back(edges[i].to);
                    }
                }
            }

            if(nodes_to_head.size() > 0) {
                assert_gt(thread_id, 0);
                assert_eq(prev_tail_nodes.size(), 0);
                writeIndex<index_t>(rg_out_file, nodes_to_head.size(), bigEndian);
                for(index_t i = 0; i < nodes_to_head.size(); i++) {
                    writeIndex<index_t>(rg_out_file, nodes_to_head[i], bigEndian);
                }
            }
        }

        // Need to check if it's reverse-deterministic
        if(num_head_nodes > 1) {
            threadParam->multipleHeadNodes = true;
        }

        // List tail nodes
        prev_tail_nodes.clear();
        if(tail_off) {
            for(index_t i = 0; i < tmp_num_edges; i++) {
                if(edges[i].to == tail_node) {
                    prev_tail_nodes.push_back(edges[i].from);
                }
            }
        }

        // Write nodes and edges
        index_t tmp_num_nodes = nodes.size();
        assert_gt(tmp_num_nodes, 2);
        if(head_off) tmp_num_nodes--;
        if(tail_off) tmp_num_nodes--;
        writeIndex<index_t>(rg_out_file, tmp_num_nodes, bigEndian);
        ASSERT_ONLY(index_t num_nodes_written = 0);
        for(index_t i = 0; i < nodes.size(); i++) {
            if(head_off && nodes[i].label == 'Y') continue;
            if(tail_off && nodes[i].label == 'Z') continue;
            nodes[i].write(rg_out_file, bigEndian);
            ASSERT_ONLY(num_nodes_written++);
        }
        assert_eq(tmp_num_nodes, num_nodes_written);
        tmp_num_edges = edges.size();
        assert_gt(tmp_num_edges, num_head_nodes + prev_tail_nodes.size());
        if(head_off) tmp_num_edges -= num_head_nodes;
        if(tail_off) tmp_num_edges -= prev_tail_nodes.size();
        writeIndex<index_t>(rg_out_file, tmp_num_edges, bigEndian);
        ASSERT_ONLY(index_t num_edges_written = 0);
        for(index_t i = 0; i < edges.size(); i++) {
            if(head_off && edges[i].from == head_node) continue;
            if(tail_off && edges[i].to == tail_node) continue;
            edges[i].write(rg_out_file, bigEndian);
            ASSERT_ONLY(num_edges_written++);
        }
        assert_eq(tmp_num_edges, num_edges_written);

        // Clear nodes and edges
        nodes.clear(); edges.clear();

        curr_pos += curr_len;
        num_nodes += tmp_num_nodes;
        num_edges += tmp_num_edges;
    }

    if(nthreads > 1 && thread_id + 1 < (index_t)nthreads && prev_tail_nodes.size() > 0) {
        writeIndex<index_t>(rg_out_file, prev_tail_nodes.size(), bigEndian);
        for(index_t i = 0; i < prev_tail_nodes.size(); i++) {
            writeIndex<index_t>(rg_out_file, prev_tail_nodes[i], bigEndian);
        }
    }

    // Close out file handle
    rg_out_file.close();
}

template <typename index_t>
bool RefGraph<index_t>::isReverseDeterministic(EList<Node>& nodes, EList<Edge>& edges)
{
    if(edges.size() <= 0) return true;

    // Sort edges by "to" nodes
    sortEdgesTo(edges);

    index_t curr_to = edges.front().to;
    EList<bool> seen; seen.resize(5); seen.fillZero();
    for(index_t i = 1; i < edges.size(); i++) {
        index_t from = edges[i].from;
        assert_lt(from, nodes.size());
        char nt = nodes[from].label;
        assert(nt == 'A' || nt == 'C' || nt == 'G' || nt == 'T' || nt == 'Y');
        if(nt == 'Y') nt = 4;
        else          nt = asc2dna[(int)nt];
        assert_lt(nt, seen.size());
        if(curr_to != edges[i].to) {
            curr_to = edges[i].to;
            seen.fillZero();
            seen[nt] = true;
        } else {
            if(seen[nt]) return false;
            seen[nt] = true;
        }
    }

    return true;
}


template <typename index_t>
void RefGraph<index_t>::reverseDeterminize(EList<Node>& nodes, EList<Edge>& edges, index_t& lastNode, index_t lastNode_add)
{
    EList<CompositeNode> cnodes; cnodes.ensure(nodes.size());
    map<CompositeNodeIDs, index_t> cnode_map;
    deque<index_t> active_cnodes;
    EList<CompositeEdge> cedges; cedges.ensure(edges.size());

    // Start from the final node ('Z')
    assert_lt(lastNode, nodes.size());
    const Node& last_node = nodes[lastNode];
    cnodes.expand();
    cnodes.back().reset();
    cnodes.back().label = last_node.label;
    cnodes.back().value = last_node.value;
    cnodes.back().nodes.push_back(lastNode);
    active_cnodes.push_back(0);
    cnode_map[cnodes.back().nodes] = 0;
    sortEdgesTo(edges);

    index_t firstNode = 0; // Y -> ... -> Z
    EList<index_t> predecessors;
    while(!active_cnodes.empty()) {
        index_t cnode_id = active_cnodes.front(); active_cnodes.pop_front();
        assert_lt(cnode_id, cnodes.size());

        // Find predecessors of this composite node
        predecessors.clear();
        for(size_t i = 0; i < cnodes[cnode_id].nodes.size(); i++) {
            index_t node_id = cnodes[cnode_id].nodes.getID(i);
            pair<index_t, index_t> edge_range = findEdgesTo(edges, node_id);
            assert_leq(edge_range.first, edge_range.second);
            assert_leq(edge_range.second, edges.size());
            for(index_t j = edge_range.first; j < edge_range.second; j++) {
                assert_eq(edges[j].to, node_id);
                predecessors.push_back(edges[j].from);
            }
        }

        if(predecessors.size() >= 2) {
            // Remove redundant nodes
            predecessors.sort();
            index_t new_size = unique(predecessors.begin(), predecessors.end()) - predecessors.begin();
            predecessors.resize(new_size);

            // Create composite nodes by labels
            stable_sort(predecessors.begin(), predecessors.end(), TempNodeLabelCmp(nodes));
        }

        for(size_t i = 0; i < predecessors.size();) {
            index_t node_id = predecessors[i];
            assert_lt(node_id, nodes.size());
            const Node& node = nodes[node_id]; i++;

            cnodes.expand();
            cnodes.back().reset();
            cnodes.back().label = node.label;
            cnodes.back().value = node.value;
            cnodes.back().nodes.push_back(node_id);

            if(node.label == 'Y' && firstNode == 0) {
                firstNode = cnodes.size() - 1;
            }

            while(i < predecessors.size()) {
                index_t next_node_id = predecessors[i];
                assert_lt(next_node_id, nodes.size());
                const Node& next_node = nodes[next_node_id];
                if(next_node.label != node.label) break;
                cnodes.back().nodes.push_back(next_node_id);
                if(next_node.value != (index_t)INDEX_MAX) {
                    if(cnodes[cnode_id].value == (index_t)INDEX_MAX) {
                        cnodes[cnode_id].value = next_node.value;
                    } else {
                        cnodes[cnode_id].value = max(cnodes[cnode_id].value, next_node.value);
                    }
                }
                i++;
            }

            // Create edges from this new composite node to current composite node
            typename map<CompositeNodeIDs, index_t>::iterator existing = cnode_map.find(cnodes.back().nodes);
            if(existing == cnode_map.end()) {
                cnode_map[cnodes.back().nodes] = cnodes.size() - 1;
                active_cnodes.push_back(cnodes.size() - 1);
                cedges.push_back(CompositeEdge(cnodes.size() - 1, cnode_id));
            } else {
                cnodes.pop_back();
                cedges.push_back(CompositeEdge((*existing).second, cnode_id));
            }

            // Increment indegree
            cnodes[cnode_id].id++;
        }
    }

    // Interchange from and to
    for(index_t i = 0; i < cedges.size(); i++) {
        index_t tmp = cedges[i].from;
        cedges[i].from = cedges[i].to;
        cedges[i].to = tmp;
    }
    sort(cedges.begin(), cedges.end());
    active_cnodes.push_back(0);
    while(!active_cnodes.empty()) {
        index_t cnode_id = active_cnodes.front(); active_cnodes.pop_front();
        assert_lt(cnode_id, cnodes.size());
        const CompositeNode& cnode = cnodes[cnode_id];
        index_t i = cedges.bsearchLoBound(CompositeEdge(cnode_id, 0));
        while(i < cedges.size()) {
            assert_geq(cedges[i].from, cnode_id);
            if(cedges[i].from != cnode_id) break;
            index_t predecessor_cnode_id = cedges[i].to;
            assert_lt(predecessor_cnode_id, cnodes.size());
            CompositeNode& predecessor_cnode = cnodes[predecessor_cnode_id];
            if(cnode.value == predecessor_cnode.value + 1) {
                active_cnodes.push_back(predecessor_cnode_id);
                break;
            }
            i++;
        }
    }
    // Restore from and to by interchanging them
    for(index_t i = 0; i < cedges.size(); i++) {
        index_t tmp = cedges[i].from;
        cedges[i].from = cedges[i].to;
        cedges[i].to = tmp;
    }

    // Create new nodes
    lastNode = 0; // Invalidate lastNode
    nodes.resizeExact(cnodes.size()); nodes.clear();
    assert_neq(firstNode, 0);
    assert_lt(firstNode, cnodes.size());
    CompositeNode& first_node = cnodes[firstNode];
    first_node.id = 0;
    nodes.expand();
    nodes.back() = first_node.getNode();
    active_cnodes.push_back(firstNode);
    sort(cedges.begin(), cedges.end());
    while(!active_cnodes.empty()) {
        index_t cnode_id = active_cnodes.front(); active_cnodes.pop_front();
        assert_lt(cnode_id, cnodes.size());
        index_t i = cedges.bsearchLoBound(CompositeEdge(cnode_id, 0));
        while(i < cedges.size()) {
            assert_geq(cedges[i].from, cnode_id);
            if(cedges[i].from != cnode_id) break;
            index_t successor_cnode_id = cedges[i].to;
            assert_lt(successor_cnode_id, cnodes.size());
            CompositeNode& successor_cnode = cnodes[successor_cnode_id];
            assert_gt(successor_cnode.id, 0);
            successor_cnode.id--;
            if(successor_cnode.id == 0) {
                active_cnodes.push_back(successor_cnode_id);
                successor_cnode.id = nodes.size();
                nodes.expand();
                nodes.back() = successor_cnode.getNode();
                if(nodes.back().label == 'Z') {
                    assert_eq(lastNode, 0);
                    assert_gt(nodes.size(), 1);
                    lastNode = nodes.size() - 1;
                }
            }
            i++;
        }
    }

    // Create new edges
    edges.resizeExact(cedges.size()); edges.clear();
    for(index_t i = 0; i < cedges.size(); i++) {
        const CompositeEdge& edge = cedges[i];
        edges.expand();
        edges.back() = edge.getEdge(cnodes);
    }
    sortEdgesFrom(edges);

#if 0
#ifndef NDEBUG
    if(debug) {
        cerr << "Nodes:" << endl;
        for(size_t i = 0; i < nodes.size(); i++) {
            const Node& node = nodes[i];
            cerr << "\t" << i << "\t" << node.label << "\t" << node.value << (node.backbone ? "\tbackbone" : "") << endl;
        }
        cerr << endl;
        cerr << "Edges: " << endl;
        for(size_t i = 0; i < edges.size(); i++) {
            const Edge& edge = edges[i];
            cerr << "\t" << i << "\t" << edge.from << " --> " << edge.to << endl;
        }
        cerr << endl;
    }
#endif
#endif
}

template <typename index_t>
class PathGraph {
public:
    struct PathNode {
        index_t                from;
        index_t                to;
        pair<index_t, index_t> key;

        void setSorted()      { to = (index_t)INDEX_MAX; }
        bool isSorted() const { return to == (index_t)INDEX_MAX; }

        index_t value() const { return to; }
        index_t outdegree() const { return key.first; }

        bool operator< (const PathNode& o) const {
            return key < o.key;
        };
    };

    static index_t PathNodeFrom (PathNode& a) {
    	return a.from;
    }

    static index_t PathNodeKey (PathNode& a) {
       	return a.key.first;
    }

    struct PathNodeFromCmp {
        bool operator() (const PathNode& a, const PathNode& b) const {
            return a.from < b.from;
        }
    };

    struct PathNodeToCmp {
    	bool operator() (const PathNode& a, const PathNode& b) const {
    		return a.to < b.to;
    	}
    };

    struct PathEdge {
        index_t from;
        index_t ranking;
        char    label;

        PathEdge() { reset(); }

        PathEdge(index_t from_, index_t ranking_, char label_) : from(from_), ranking(ranking_), label(label_) {}

        void reset() {
            from = 0;
            ranking = 0;
            label = 0;
        }

        bool operator< (const PathEdge& o) const {
            return label < o.label || (label == o.label && ranking < o.ranking);
        };

    };
    static index_t PathEdgeTo (PathEdge& a) {
        return a.ranking;
    }

    struct PathEdgeFromCmp {
        bool operator() (const PathEdge& a, const PathEdge& b) const {
            return a.from < b.from || (a.from == b.from && a.ranking < b.ranking);
        }
    };


    struct PathEdgeToCmp {
        bool operator() (const PathEdge& a, const PathEdge& b) const {
            return a.ranking < b.ranking;
        }
    };

    struct TempEdge {
        index_t from; // from Node
        index_t to;   // to Node

    };

    struct TempEdgeFromCmp {
        bool operator() (const TempEdge& a, const TempEdge& b) const {
            return a.from < b.from;
        }
    };

    struct TempEdgeToCmp {
        bool operator() (const TempEdge& a, const TempEdge& b) const {
            return a.to < b.to;
        };
    };

private:
    struct ThreadParam {
        PathGraph<index_t>*              previous;
        EList<PathNode>*                 from_nodes;
        EList<PathNode>*			     to_nodes;
        EList<index_t>*                  from_cur;
        EList<index_t>*                  to_cur;
        index_t                          st;
        index_t                          en;
        int                              partitions;
        int                              thread_id;
     };
    static void seperateNodes(void* vp);
    static void seperateNodesCount(void* vp);
    static index_t hash(index_t id, index_t div) {
        uint64_t mult = 13;
        return (mult * (uint64_t)id) % div;
    }


    struct ThreadParam2 {
        index_t                         st;
        index_t                         en;
        EList<PathNode>*                from_nodes;
        EList<PathNode>*     	         to_nodes;
        EList<index_t>*                 from_size;
        EList<index_t>*                 to_size;
        EList<index_t>*                 from_off;
        EList<index_t>*                 to_off;
        int                             thread_id;
        PathNode**                      new_nodes;
        EList<index_t>*                 new_cur;
        EList<index_t>*                 breakvalues;
        ELList<index_t>*                breakpoints;
        int                             nthreads;
        int                             generation;
    };
    static void createCombined(void* vp);
    
    struct ThreadParam3 {
        PathGraph<index_t>*             previous;
        int                             thread_id;
        PathNode**                      new_nodes;
        ELList<index_t>*                breakpoints;
        int                             nthreads;
        PathNode**                      merged_nodes;
        index_t*                        merged_cur;
        index_t*                        mranks;
        tthread::mutex*                 merge_mutex;
    };
    static void mergeNodes(void* vp);
    
    struct ThreadParam4 {
        RefGraph<index_t>*                base;
        int                               thread_id;
        int								  st;
        int                               en;
        EList<PathNode>*                  sep_nodes;
        EList<TempEdge>*                  sep_edges;
        EList<PathEdge>*                  new_edges;
    };
    static void generateEdgesWorker(void* vp);


public:
    // Create a new graph in which paths are represented using nodes
    PathGraph(RefGraph<index_t>& parent, int nthreads_ = 1, bool verbose_ = false);

    // Construct a next 2^(j+1) path graph from a 2^j path graph
    PathGraph(PathGraph<index_t>& previous);

    ~PathGraph() {}

    void printInfo();

    bool generateEdges(RefGraph<index_t>& parent);

    index_t getNumNodes() const { return nodes.size(); }
    index_t getNumEdges() const { return edges.size(); }
    
    bool isSorted() const { return sorted; }

    //
    bool nextRow(int& gbwtChar, int& F, int& M, index_t& pos) {
        if(report_node_idx >= nodes.size()) return false;
        bool firstOutEdge = false;
        if(report_edge_range.first >= report_edge_range.second) {
            report_edge_range = getEdges(report_node_idx, false /* from? */);
            firstOutEdge = true;
            if(report_node_idx == 0) {
                report_M = pair<index_t, index_t>(0, 0);
            }
        }
        assert_lt(report_edge_range.first, report_edge_range.second);
        assert_lt(report_edge_range.first, edges.size());
        const PathEdge& edge = edges[report_edge_range.first];
        gbwtChar = edge.label;
        if(gbwtChar == 'Y') gbwtChar = 'Z';
        assert_lt(report_node_idx, nodes.size());
        F = (firstOutEdge ? 1 : 0);

        report_edge_range.first++;
        if(report_edge_range.first >= report_edge_range.second) {
            report_node_idx++;
        }
        assert_lt(report_M.first, nodes.size());
        pos = nodes[report_M.first].to;
        M = (report_M.second == 0 ? 1 : 0);
        report_M.second++;
        if(report_M.second >= nodes[report_M.first].key.first) {
            report_M.first++;
            report_M.second = 0;
        }
        return true;
    }

    //
    index_t nextFLocation() {
        if(report_F_node_idx >= nodes.size()) return (index_t)INDEX_MAX;
        index_t ret = report_F_location;
        pair<index_t, index_t> edge_range = getEdges(report_F_node_idx, false /* from? */);
        report_F_node_idx++;
        assert_lt(edge_range.first, edge_range.second);
        report_F_location += (edge_range.second - edge_range.first);
        return ret;
    }

private:
    void makeFromRef(RefGraph<index_t>& base);
    void generationOne();
    void earlyGeneration();
    void firstPruneGeneration();
    void lateGeneration();

    void mergeUpdateRank();

    pair<index_t, index_t> nextMaximalSet(pair<index_t, index_t> range);

    void sortByKey() { sort(nodes.begin(), nodes.end()); } // by key

    // Can create an index by using key.second in PathNodes.
    void sortNodesByFrom() {
    	bin_sort_no_copy<PathNode, PathNodeFromCmp, index_t>(
    				nodes.begin(), nodes.end(), &PathNodeFrom, max_from, nthreads);
    }
    pair<index_t, index_t> getNodesFrom(index_t node);        // Use sortByFrom(true) first.

private:
    int             nthreads;
    bool            verbose;

    EList<PathNode> past_nodes;
    EList<PathNode> nodes;
    EList<PathEdge> edges;
    index_t         ranks;
    index_t         max_label;  // Node label of initial nodes.
    index_t         max_from; //number of nodes in RefGraph
    index_t         temp_nodes; // Total number of nodes created before sorting.


    index_t         generation; // Sorted by paths of length 2^generation.
    bool            sorted;
    
    // For reporting GBWT char, F, and M values
    index_t                report_node_idx;
    pair<index_t, index_t> report_edge_range;
    pair<index_t, index_t> report_M;
    // For reporting location in F corresponding to 1 bit in M
    index_t                report_F_node_idx;
    index_t                report_F_location;

    void      sortEdges(bool by_from, bool create_index);
    pair<index_t, index_t> getEdges(index_t node, bool by_from); // Create index first.

    // following variables are for debugging purposes
#ifndef NDEBUG
    bool               debug;
#endif

    EList<char>        bwt_string;
    EList<char>        F_array;
    EList<char>        M_array;
    EList<index_t>     bwt_counts;

    // brute-force implementations
    index_t select(const EList<char>& array, index_t p, char c) {
        if(p <= 0) return 0;
        for(index_t i = 0; i < array.size(); i++) {
            if(array[i] == c) {
                assert_gt(p, 0);
                p--;
                if(p == 0)
                    return i;
            }
        }
        return array.size();
    }

    index_t select1(const EList<char>& array, index_t p) {
        return select(array, p, 1);
    }

    index_t rank(const EList<char>& array, index_t p, char c) {
        index_t count = 0;
        assert_lt(p, array.size());
        for(index_t i = 0; i <= p; i++) {
            if(array[i] == c)
                count++;
        }
        return count;
    }

    index_t rank1(const EList<char>& array, index_t p) {
        return rank(array, p, 1);
    }

    // for debugging purposes
#ifndef NDEBUG
public: EList<pair<index_t, index_t> > ftab;
#endif
};

//Creates an initial PathGRaph from the RefGraph
//All nodes begin as unsorted nodes
template <typename index_t>
PathGraph<index_t>::PathGraph(RefGraph<index_t>& base, int nthreads_, bool verbose_) :
nthreads(nthreads_), verbose(verbose_),
ranks(0), max_label('Z'), temp_nodes(0), generation(0), sorted(false),
report_node_idx(0), report_edge_range(pair<index_t, index_t>(0, 0)), report_M(pair<index_t, index_t>(0, 0)),
report_F_node_idx(0), report_F_location(0)
{
#ifndef NDEBUG
    debug = base.nodes.size() <= 20;
#endif

    //fill nodes and set max_from
    makeFromRef(base);

    generationOne();

    while(generation < 3) { // this needs to be changed to account for different size index_t's
    	earlyGeneration();
    }

    firstPruneGeneration();
    if(isSorted()) return;
    past_nodes.swap(nodes);

    while(true) {
    	lateGeneration();
    	if(isSorted()) break;
    	past_nodes.swap(nodes);
	}
}

template <typename index_t>
void PathGraph<index_t>::makeFromRef(RefGraph<index_t>& base) {
	// Create a path node per edge with a key set to from node's label
	temp_nodes = base.edges.size() + 1;
	max_from = temp_nodes + 2;
	nodes.reserveExact(temp_nodes);
	for(index_t i = 0; i < base.edges.size(); i++) {
		const typename RefGraph<index_t>::Edge& e = base.edges[i];
		nodes.expand();
		nodes.back().from = e.from;
		nodes.back().to = e.to;

		switch(base.nodes[e.from].label) {
		case 'A':
			nodes.back().key = pair<index_t, index_t>(0, 0);
			break;
		case 'C':
			nodes.back().key = pair<index_t, index_t>(1, 0);
			break;
		case 'G':
			nodes.back().key = pair<index_t, index_t>(2, 0);
			break;
		case 'T':
			nodes.back().key = pair<index_t, index_t>(3, 0);
			break;
		case 'Y':
			nodes.back().key = pair<index_t, index_t>(4, 0);
			break;
		default:
			assert(false);
			throw 1;
		}
	}
	// Final node.
	assert_lt(base.lastNode, base.nodes.size());
	assert_eq(base.nodes[base.lastNode].label, 'Z');
	nodes.expand();
	nodes.back().from = nodes.back().to = base.lastNode;
	nodes.back().key = pair<index_t, index_t>(5, 0);

	printInfo();
}

template <typename index_t>
void PathGraph<index_t>::generationOne() {
	generation++;
	// first count where to start each from value
	EList<index_t> from_index;
	from_index.resizeExact(max_from + 1);
	from_index.fillZero();
	//Build from_index
	for(PathNode* node = nodes.begin(); node != nodes.end(); node++) {
		from_index[node->from]++;
	}
	index_t tot = from_index[0];
	from_index[0] = 0;
	for(index_t i = 1; i < max_from + 1; i++) {
		tot += from_index[i];
		from_index[i] = tot - from_index[i];
	}

	// use past_nodes as from_table
	past_nodes.resizeExact(nodes.size());
	past_nodes.fillZero();

	for(PathNode* node = nodes.begin(); node != nodes.end(); node++) {
		past_nodes[from_index[node->from]++] = *node;
	}

	//reset index
	for(index_t i = from_index.size() - 1; i > 0; i--) {
		from_index[i] = from_index[i - 1];
	}
	from_index[0] = 0;
	//Now query direct access table
	temp_nodes = 0;
	for(PathNode* node = past_nodes.begin(); node != past_nodes.end(); node++) {
		temp_nodes += from_index[node->to + 1] - from_index[node->to];
	}
	nodes.resizeExact(temp_nodes);
	nodes.clear();
	for(PathNode* node = past_nodes.begin(); node != past_nodes.end(); node++) {
		for(index_t j = from_index[node->to]; j < from_index[node->to + 1]; j++) {
			nodes.expand();
			nodes.back().from = node->from;
			nodes.back().to = past_nodes[j].to;
			assert_gt(generation, 0);
			index_t bit_shift = 1 << (generation - 1);
			bit_shift = (bit_shift << 1) + bit_shift; // Multiply by 3
			nodes.back().key  = pair<index_t, index_t>((node->key.first << bit_shift) + past_nodes[j].key.first, 0);
		}
	}
	printInfo();
	past_nodes.swap(nodes);
}

template <typename index_t>
void PathGraph<index_t>::earlyGeneration() {
	generation++;
	//here past_nodes is already sorted by .from
	// first count where to start each from value
	EList<index_t> from_index;
	from_index.resizeExact(max_from + 1);
	from_index.fillZero();

	//Build from_index
	for(index_t i = 0; i < past_nodes.size(); i++) {
		from_index[past_nodes[i].from + 1] = i + 1;
	}

	// Now query against hash-table
	//count number of nodes
	temp_nodes = 0;
	for(PathNode* node = past_nodes.begin(); node != past_nodes.end(); node++) {
		temp_nodes += from_index[node->to + 1] - from_index[node->to];
	}
	// make new nodes
	nodes.resizeExact(temp_nodes);
	nodes.clear();
	for(PathNode* node = past_nodes.begin(); node != past_nodes.end(); node++) {
		for(index_t j = from_index[node->to]; j < from_index[node->to + 1]; j++) {
			nodes.expand();
			nodes.back().from = node->from;
			nodes.back().to = past_nodes[j].to;
			assert_gt(generation, 0);
			index_t bit_shift = 1 << (generation - 1);
			bit_shift = (bit_shift << 1) + bit_shift; // Multiply by 3
			nodes.back().key  = pair<index_t, index_t>((node->key.first << bit_shift) + past_nodes[j].key.first, 0);
		}
	}
	printInfo();
	past_nodes.swap(nodes);
}

template <typename index_t>
void PathGraph<index_t>::firstPruneGeneration() {
	generation++;
	//here past_nodes is already sorted by .from
	// first count where to start each from value
	EList<index_t> from_index;
	from_index.resizeExact(max_from + 1);
	from_index.fillZero();

	//Build from_index
	for(index_t i = 0; i < past_nodes.size(); i++) {
		from_index[past_nodes[i].from + 1] = i + 1;
	}

	// Now query against hash-table
	//count number of nodes
	temp_nodes = 0;
	for(PathNode* node = past_nodes.begin(); node != past_nodes.end(); node++) {
		temp_nodes += from_index[node->to + 1] - from_index[node->to];
	}
	// make new nodes
	nodes.resizeExact(temp_nodes);
	nodes.clear();
	for(PathNode* node = past_nodes.begin(); node != past_nodes.end(); node++) {
		for(index_t j = from_index[node->to]; j < from_index[node->to + 1]; j++) {
			nodes.expand();
			nodes.back().from = node->from;
			nodes.back().to = past_nodes[j].to;
			assert_gt(generation, 0);
			nodes.back().key  = pair<index_t, index_t>(node->key.first, past_nodes[j].key.first);
		}
	}
	past_nodes.resizeExact(nodes.size());
	bin_sort_copy<PathNode, less<PathNode>, index_t>(nodes.begin(), nodes.end(), past_nodes.ptr(), &PathNodeKey, (index_t)-1, nthreads);
	nodes.swap(past_nodes);
	mergeUpdateRank();

	printInfo();
}


template <typename index_t>
void PathGraph<index_t>::lateGeneration() {
	generation++;
	clock_t overall = clock();
	clock_t indiv = clock();
	assert_gt(nthreads, 0);

	assert_neq(past_nodes.size(), ranks);

	EList<PathNode> from_table; from_table.resizeExact(past_nodes.size());
	if(verbose) cerr << "ALLOCATE FROM_TABLE: " << (float)(clock() - indiv) / CLOCKS_PER_SEC << endl;
	indiv = clock();
	bin_sort_copy<PathNode, PathNodeFromCmp, index_t>(past_nodes.begin(), past_nodes.end(), from_table.ptr(), &PathNodeFrom, max_from, nthreads);
	if(verbose) cerr << "BUILD TABLE: " << (float)(clock() - indiv) / CLOCKS_PER_SEC << endl;
	indiv = clock();

	//Build from_index
	EList<index_t> from_index; from_index.resizeExact(max_from + 1); from_index.fillZero();
	for(index_t i = 0; i < past_nodes.size(); i++) {
		from_index[from_table[i].from + 1] = i + 1;
	}
	if(verbose) cerr << "BUILD INDEX: " << (float)(clock() - indiv) / CLOCKS_PER_SEC << endl;
	indiv = clock();


	// Now query against hash-table

	//count number of nodes
	temp_nodes = 0;
	for(PathNode* node = past_nodes.begin(); node != past_nodes.end(); node++) {
		if(node->isSorted()) {
			temp_nodes++;
		} else {
			temp_nodes += from_index[node->to + 1] - from_index[node->to];
		}
	}
	if(verbose) cerr << "COUNT NEW NODES: " << (float)(clock() - indiv) / CLOCKS_PER_SEC << endl;
	indiv = clock();
	// make new nodes
	nodes.resizeExact(temp_nodes);
	nodes.clear();
	for(PathNode* node = past_nodes.begin(); node != past_nodes.end(); node++) {
		if(node->isSorted()) {
			nodes.push_back(*node);
		} else {
			for(index_t j = from_index[node->to]; j < from_index[node->to + 1]; j++) {
				nodes.expand();
				nodes.back().from = node->from;
				nodes.back().to = from_table[j].to;
				nodes.back().key  = pair<index_t, index_t>(node->key.first, from_table[j].key.first);
			}
		}
	}
	if(verbose) cerr << "MADE NEW NODES: " << (float)(clock() - indiv) / CLOCKS_PER_SEC << endl;
	indiv = clock();
	// Now make all nodes properly sorted
	PathNode* block_start = nodes.begin();
	PathNode* curr = nodes.begin();
	ranks = 0;
	for(PathNode* node = nodes.begin() + 1; node != nodes.end(); node++) {
		if(node->key.first != block_start->key.first) {
			if(node - block_start > 1) {
				sort(block_start, node);
				while(block_start != node) {
					//extend while share same key
					index_t shift = 1;
					while(block_start + shift != node && block_start->key == (block_start + shift)->key) {
						shift++;
					}
					//check if all share .from
					bool merge = true;
					for(PathNode* n = block_start; n != (block_start + shift) && merge; n++) {
						if(n->from != block_start->from) merge = false;
					}
					//if they share same from, then they are a mergable set
					//check if they are able to be merged into the previous node (second clause)
					//otherwise write single node to array

					//if not mergable, just write all to array
					if(!merge) {
						for(PathNode* n = block_start; n != (block_start + shift); n++) {
							n->key.first = ranks;
							*curr++ = *n;
						}
						ranks++;
					} else if(curr == nodes.begin() || !(curr - 1)->isSorted() || (curr - 1)->from != block_start->from){
						block_start->setSorted();
						block_start->key.first = ranks++;
						*curr++ = *block_start;
					}
					block_start += shift;
				}
			} else if(curr == nodes.begin() || (!(curr - 1)->isSorted() || (curr - 1)->from != block_start->from)){
				block_start->key.first = ranks++;
				*curr++ = *block_start;
			}
			block_start = node;
		}
	}
	PathNode* node = nodes.end();
	sort(block_start, node);
	while(block_start != node) {
		//extend while share same key
		index_t shift = 1;
		while(block_start + shift != node && block_start->key == (block_start + shift)->key) {
			shift++;
		}
		//check if all share .from
		bool merge = true;
		for(PathNode* n = block_start; n != (block_start + shift) && merge; n++) {
			if(n->from != block_start->from) merge = false;
		}
		//if they share same from, then they are a mergable set
		//check if they are able to be merged into the previous node (second clause)
		//otherwise write single node to array

		//if not mergable, just write all to array
		if(!merge) {
			for(PathNode* n = block_start; n != (block_start + shift); n++) {
				n->key.first = ranks;
				*curr++ = *n;
			}
			ranks++;
		} else if(!(curr - 1)->isSorted() || (curr - 1)->from != block_start->from){
			block_start->setSorted();
			block_start->key.first = ranks++;
			*curr++ = *block_start;
		}
		block_start += shift;
	}
	nodes.resizeExact(curr - nodes.begin());
	if(nodes.end() - block_start > 1) sort(block_start, nodes.end());
	if(verbose) cerr << "SORTED ALL NODES: " << (float)(clock() - indiv) / CLOCKS_PER_SEC << endl;
	indiv = clock();
	mergeUpdateRank();
	if(verbose) cerr << "MERGEUPDATERANK: " << (float)(clock() - indiv) / CLOCKS_PER_SEC << endl;
	if(verbose) cerr << "TOTAL TIME: " << (float)(clock() - overall) / CLOCKS_PER_SEC << endl;
	printInfo();
}


template <typename index_t>
void PathGraph<index_t>::printInfo()
{
    if(verbose) {
        cerr << "Generation " << generation
        << " (" << temp_nodes << " -> " << nodes.size() << " nodes, "
        << ranks << " ranks    	int bin = ;)" << endl;
    }
}

//--------------------------------------------------------------------------
template <typename index_t>
bool PathGraph<index_t>::generateEdges(RefGraph<index_t>& base)
{
	//entering we have:
	// nodes - sorted by rank
	// edges - empty
	// base.nodes - almost sorted by from/to
	// base.edges - almost sorted by from/to

	//need to join:
	// nodes.from -> base.nodes[]
	// nodes.from -> base.edges.to
	// nodes.from -> edges.from

	if(!sorted) return false;

	time_t indiv = time(0);
	time_t overall = time(0);

	//sort nodes by from using in-place radix sort
	//could switch to out of place and get speed increase, if have space
	sortNodesByFrom();

	if(verbose) cerr << "Sort nodes by from: "  << time(0) - indiv << endl;
	indiv = time(0);

	//replace nodes.to with genomic position
	//fast because both roughly ordered by from
	for(PathNode* node = nodes.begin(); node != nodes.end(); node++) {
		node->to = base.nodes[node->from].value;
	}

	if(verbose) cerr << "NODE.TO -> GENOME POS: "  << time(0) - indiv << endl;
	indiv = time(0);

	// build an index for nodes
	EList<index_t> from_index; from_index.resizeExact(max_from + 1); from_index.fillZero();
	for(index_t i = 0; i < nodes.size(); i++) {
		from_index[nodes[i].from + 1] = i + 1;
	}

	if(verbose) cerr << "BUILD FROM_INDEX "  << time(0) - indiv << endl;
	indiv = time(0);

	// Now join nodes.from to edges.to
	// fast because base.edges roughly sorted by to

	//count number of edges                                                                 //serial, need to parallelize
	index_t label_index[6] = {0};
	for(typename RefGraph<index_t>::Edge* edge = base.edges.begin(); edge != base.edges.end(); edge++) {
		char curr_label = base.nodes[edge->from].label;
		int curr_label_index;
		switch(curr_label) {
			case 'A': curr_label_index = 0; break;
			case 'C': curr_label_index = 1; break;
			case 'G': curr_label_index = 2; break;
			case 'T': curr_label_index = 3; break;
			case 'Y': curr_label_index = 4; break;
			case 'Z': curr_label_index = 5; break;
			default: assert(false); throw 1;
		}
		label_index[curr_label_index] += from_index[edge->to + 1] - from_index[edge->to];
	}
	// make new nodes
	index_t tot = label_index[0];
	label_index[0] = 0;
	for(int i = 1; i < 6; i++) {
		tot += label_index[i];
		label_index[i] =  tot - label_index[i];
	}

	if(verbose) cerr << "COUNT NEW EDGES: " << time(0) - indiv << endl;
	indiv = time(0);

	edges.resizeExact(tot);
	for(typename RefGraph<index_t>::Edge* edge = base.edges.begin(); edge != base.edges.end(); edge++) {
		char curr_label = base.nodes[edge->from].label;
		int curr_label_index;
		switch(curr_label) {
			case 'A': curr_label_index = 0; break;
			case 'C': curr_label_index = 1; break;
			case 'G': curr_label_index = 2; break;
			case 'T': curr_label_index = 3; break;
			case 'Y': curr_label_index = 4; break;
			case 'Z': curr_label_index = 5; break;
			default: assert(false); throw 1;
		}
		for(index_t j = from_index[edge->to]; j < from_index[edge->to + 1]; j++) {
			edges[label_index[curr_label_index]++] = PathEdge(edge->from, nodes[j].key.first, curr_label);
		}
	}

	if(verbose) cerr << "MADE NEW EDGES: " << time(0) - indiv << endl;
	indiv = time(0);

	// know breakpoints

	bin_sort_no_copy<PathEdge, less<PathEdge>, index_t>(edges.begin(), edges.begin() + label_index[0], &PathEdgeTo, edges.size());
	bin_sort_no_copy<PathEdge, less<PathEdge>, index_t>(edges.begin() + label_index[0], edges.begin() + label_index[1], &PathEdgeTo, edges.size());
	bin_sort_no_copy<PathEdge, less<PathEdge>, index_t>(edges.begin() + label_index[1], edges.begin() + label_index[2], &PathEdgeTo, edges.size());
	bin_sort_no_copy<PathEdge, less<PathEdge>, index_t>(edges.begin() + label_index[2], edges.begin() + label_index[3], &PathEdgeTo, edges.size());

	sort(edges.begin() + label_index[3], edges.begin() + label_index[4]);
	sort(edges.begin() + label_index[4], edges.begin() + label_index[5]);

	if(verbose) cerr << "SORTED NEW EDGES: " << time(0) - indiv << endl;
	indiv = time(0);

	bin_sort_no_copy<PathNode, less<PathNode>, index_t>(nodes.begin(), nodes.end(), &PathNodeKey, ranks, nthreads);

	if(verbose) cerr << "RE-SORTED NODES: " << time(0) - indiv << endl;
	indiv = time(0);

#ifndef NDEBUG
    if(debug) {
        cerr << "just after creating path edges" << endl;
        cerr << "Ref edges" << endl;
        for(size_t i = 0; i < base.edges.size(); i++) {
            const typename RefGraph<index_t>::Edge& edge = base.edges[i];
            cerr << "\t" << i << "\t" << edge.from << " --> " << edge.to << endl;
        }

        cerr << "Path nodes" << endl;
        for(size_t i = 0; i < nodes.size(); i++) {
            const PathNode& node = nodes[i];
            cerr << "\t" << i << "\t(" << node.key.first << ", " << node.key.second << ")\t"
            << node.from << " --> " << node.to << endl;
        }

        cerr << "Path edges" << endl;
        for(size_t i = 0; i < edges.size(); i++) {
            const PathEdge& edge = edges[i];
            cerr << "\t" << i << "\tfrom: " << edge.from << "\tranking: " << edge.ranking << "\t" << edge.label << endl;
        }
    }
#endif

#ifndef NDEBUG

    // Switch char array[x][y]; to char** array;
    if(debug) {
        cerr << "after sorting nodes by ranking and edges by label and ranking" << endl;
        cerr << "Path nodes" << endl;
        for(size_t i = 0; i < nodes.size(); i++) {
            const PathNode& node = nodes[i];
            cerr << "\t" << i << "\t(" << node.key.first << ", " << node.key.second << ")\t"
                 << node.from << " --> " << node.to << endl;
        }

        cerr << "Path edges" << endl;
        for(size_t i = 0; i < edges.size(); i++) {
            const PathEdge& edge = edges[i];
            cerr << "\t" << i << "\tfrom: " << edge.from << "\tranking: " << edge.ranking << "\t" << edge.label << endl;
        }
    }
#endif

    // Sets PathNode.to = GraphNode.value and PathNode.key.first to outdegree
    // Replaces (from.from, to) with (from, to)
    PathNode* node = nodes.begin(); node->key.first = 0;
    PathEdge* edge = edges.begin();
    while(node != nodes.end() && edge != edges.end()) {
        if(edge->from == node->from) {
            edge->from = node - nodes.begin(); edge++;
            node->key.first++;
        } else {
            node++; node->key.first = 0;
        }
    }

    if(verbose) cerr << "PROCESS EDGES: " << time(0) - indiv << endl;
	indiv = time(0);

    // Remove 'Y' node
    assert_gt(nodes.size(), 2);
    nodes.back().key.first = nodes[nodes.size() - 2].key.first;
    nodes[nodes.size() - 2] = nodes.back();
    nodes.pop_back();
    // Adjust edges accordingly
    for(size_t i = 0; i < edges.size(); i++) {
        PathEdge& edge = edges[i];
        if(edge.label == 'Y') {
            edge.label = 'Z';
        } else if(edge.ranking >= nodes.size()) {
            assert_eq(edge.ranking, nodes.size());
            edge.ranking -= 1;
        }
    }
    if(verbose) cerr << "REMOVE Y: " << time(0) - indiv << endl;
	indiv = time(0);

#ifndef NDEBUG
    if(debug) {
        cerr << "Path nodes" << endl;
        for(size_t i = 0; i < nodes.size(); i++) {
            const PathNode& node = nodes[i];
            cerr << "\t" << i << "\t(" << node.key.first << ", " << node.key.second << ")\t"
            << node.from << " --> " << node.to << endl;
        }

        cerr << "Path edges" << endl;
        for(size_t i = 0; i < edges.size(); i++) {
            const PathEdge& edge = edges[i];
            cerr << "\t" << i << "\tfrom: " << edge.from << "\tranking: " << edge.ranking << "\t" << edge.label << endl;
        }
    }
#endif

    // this should be a merge not a sort!!
    // edges with a given label are sorted by ranking
    // only 4 labels so should be pretty trivial to do a 4-way merge
    bin_sort_no_copy<PathEdge, PathEdgeToCmp, index_t>(edges.begin(), edges.end(), &PathEdgeTo, edges.size(), nthreads);
    for(index_t i = 0; i < edges.size(); i++) {
        nodes[edges[i].ranking].key.second = i + 1;
    }

    if(verbose) cerr << "SORT, Make index: " << time(0) - indiv << endl;
    if(verbose) cerr << "TOTAL: " << time(0) - overall << endl;
    return true;

//-----------------------------------------------------------------------------------------------------
    bwt_string.clear();
    F_array.clear();
    M_array.clear();
    bwt_counts.resizeExact(5); bwt_counts.fillZero();
    for(index_t node = 0; node < nodes.size(); node++) {
        pair<index_t, index_t> edge_range = getEdges(node, false /* from? */);
        for(index_t i = edge_range.first; i < edge_range.second; i++) {
            assert_lt(i, edges.size());
            char label = edges[i].label;
            if(label == 'Y') {
                label = 'Z';
            }
            bwt_string.push_back(label);
            F_array.push_back(i == edge_range.first ? 1 : 0);

            if(label != 'Z') {
                char nt = asc2dna[(int)label];
                assert_lt(nt + 1, bwt_counts.size());
                bwt_counts[nt + 1]++;
            }
        }
        for(index_t i = 0; i < nodes[node].key.first; i++) {
            M_array.push_back(i == 0 ? 1 : 0);
        }
    }
    assert_gt(bwt_string.size(), 0);
    assert_eq(bwt_string.size(), F_array.size());
    assert_eq(bwt_string.size(), M_array.size());

    for(size_t i = 0; i < bwt_counts.size(); i++) {
        if(i > 0) bwt_counts[i] += bwt_counts[i - 1];
    }

#ifndef NDEBUG
    if(debug) {
        cerr << "Path nodes (final)" << endl;
        for(size_t i = 0; i < nodes.size(); i++) {
            const PathNode& node = nodes[i];
            cerr << "\t" << i << "\t(" << node.key.first << ", " << node.key.second << ")\t"
            << node.from << " --> " << node.to << endl;
        }

        cerr << "Path edges (final)" << endl;
        for(size_t i = 0; i < edges.size(); i++) {
            const PathEdge& edge = edges[i];
            cerr << "\t" << i << "\tfrom: " << edge.from << "\tranking: " << edge.ranking << "\t" << edge.label << endl;
        }

        cerr << "i\tBWT\tF\tM" << endl;
        for(index_t i = 0; i < bwt_string.size(); i++) {
            cerr << i << "\t" << bwt_string[i] << "\t"  // BWT char
            << (int)F_array[i] << "\t"             // F bit value
            << (int)M_array[i] << endl;            // M bit value
        }

        for(size_t i = 0; i < bwt_counts.size(); i++) {
            cerr << i << "\t" << bwt_counts[i] << endl;
        }
    }
#endif

    // Test searches, based on paper_example
#if 1
    EList<string> queries;  EList<index_t> answers;
#   if 1
#      if 1
    queries.push_back("GACGT"); answers.push_back(9);
    queries.push_back("GATGT"); answers.push_back(9);
    queries.push_back("GACT");  answers.push_back(9);
    queries.push_back("ATGT");  answers.push_back(4);
    queries.push_back("GTAC");  answers.push_back(10);
    queries.push_back("ACTG");  answers.push_back(3);
#      else
    // rs55902548, at 402, ref, alt, unknown alt
    queries.push_back("GGCAGCTCCCATGGGTACACACTGGGCCCAGAACTGGGATGGAGGATGCA");
    // queries.push_back("GGCAGCTCCCATGGGTACACACTGGTCCCAGAACTGGGATGGAGGATGCA");
    // queries.push_back("GGCAGCTCCCATGGGTACACACTGGACCCAGAACTGGGATGGAGGATGCA");

    // rs5759268, at 926787, ref, alt, unknown alt
    // queries.push_back("AAATTGCTCAGCCTTGTGCTGTGCACACCTGGTTCTCTTTCCAGTGTTAT");
    // queries.push_back("AAATTGCTCAGCCTTGTGCTGTGCATACCTGGTTCTCTTTCCAGTGTTAT");
    // queries.push_back("AAATTGCTCAGCCTTGTGCTGTGCAGACCTGGTTCTCTTTCCAGTGTTAT");
#      endif

    for(size_t q = 0; q < queries.size(); q++) {
        const string& query = queries[q];
        assert_gt(query.length(), 0);
        index_t top = 0, bot = edges.size();
        index_t node_top = 0, node_bot = 0;
        cerr << "Aligning " << query <<  endl;
        index_t i = 0;
        for(; i < query.length(); i++) {
            if(top >= bot) break;
            int nt = query[query.length() - i - 1];
            nt = asc2dna[nt];
            assert_lt(nt, 4);

            cerr << "\t" << i << "\tBWT range: [" << top << ", " << bot << ")" << endl;

            top = bwt_counts[(int)nt] + (top <= 0 ? 0 : rank(bwt_string, top - 1, "ACGT"[nt]));
            bot = bwt_counts[(int)nt] + rank(bwt_string, bot - 1, "ACGT"[nt]);
            cerr << "\t\tLF BWT range: [" << top << ", " << bot << ")" << endl;

            node_top = rank1(M_array, top) - 1;
            node_bot = rank1(M_array, bot - 1);
            cerr << "\t\tnode range: [" << node_top << ", " << node_bot << ")" << endl;

            top = select1(F_array, node_top + 1);
            bot = select1(F_array, node_bot + 1);
        }
        cerr << "\t" << i << "\tBWT range: [" << top << ", " << bot << ")" << endl;
        // assert_eq(top, answers[q]);
        cerr << "finished... ";
        if(node_top < node_bot && node_top < nodes.size()) {
            index_t pos = nodes[node_top].to;
            index_t gpos = pos;
            const EList<RefRecord>& szs = base.szs;
            for(index_t i = 0; i < szs.size(); i++) {
                gpos += szs[i].off;
                if(pos < szs[i].len) break;
                pos -= szs[i].len;
            }

            cerr << "being aligned at " << gpos;
        }
        cerr << endl << endl;
    }
#   endif

    // See inconsistencies between F and M arraystimy thread
#   if 0
    cerr << endl << endl;
    EList<index_t> tmp_F;
    for(index_t i = 0; i < F_array.size(); i++) {
        if(F_array[i] == 1) tmp_F.push_back(i);
    }

    EList<index_t> tmp_M;
    for(index_t i = 0; i < M_array.size(); i++) {
        if(M_array[i] == 1) tmp_M.push_back(i);
    }

    index_t max_diff = 0;
    assert_eq(tmp_F.size(), tmp_M.size());
    for(index_t i = 0; i < tmp_F.size(); i++) {
        index_t diff = (tmp_F[i] >= tmp_M[i] ? tmp_F[i] - tmp_M[i] : tmp_M[i] - tmp_F[i]);
        if(diff > max_diff) {
            max_diff = diff;
            cerr << i << "\tdiff: " << max_diff << "\t" << (tmp_F[i] >= tmp_M[i] ? "+" : "-") << endl;
        }
    }
    cerr << "Final: " << tmp_F.back() << " vs. " << tmp_M.back() << endl;
#   endif

#endif
    return true;
}

//--------------------------------------------------------------------------

template <typename index_t>
void PathGraph<index_t>::mergeUpdateRank()
{
    // Update ranks
    if(generation == 4) {
    index_t rank = 0;
    pair<index_t, index_t> key = nodes.front().key;
    for(index_t i = 0; i < nodes.size(); i++) {
        PathNode& node = nodes[i];
        if(node.key != key) {
            key = node.key;
            rank++;
        }
        node.key = pair<index_t, index_t>(rank, 0);
	}
    ranks = rank + 1;
    // Merge equivalent nodes

    index_t curr = 0;
    pair<index_t, index_t> range(0, 0); // Empty range
    while(true) {
        range = nextMaximalSet(range);
        if(range.first >= range.second)
            break;
        nodes[curr] = nodes[range.first]; curr++;
    }
    nodes.resize(curr);

    // Set nodes that become sorted as sorted
    PathNode* candidate = &nodes.front();
    key = candidate->key; ranks = 1;
    for(index_t i = 1; i < nodes.size(); i++) {
        if(nodes[i].key != key) {
            if(candidate != NULL) {
                candidate->setSorted();
            }
            candidate = &nodes[i];
            key = candidate->key; ranks++;
        } else {
            candidate = NULL;
        }
    }

    if(candidate != NULL) {
        candidate->setSorted();
    }
    }
    // Only done on last iteration
    // Replace the ranks of a sorted graph so that rank(i) = i
    // Merges may otherwise leave gaps in the ranks
    if(ranks == nodes.size()) {
        for(index_t i = 0; i < nodes.size(); i++) {
            nodes[i].key.first = i;
        }
        sorted = true;
    }
}


// Returns the next maximal mergeable set of PathNodes.
// A set of PathNodes sharing adjacent keys is mergeable, if each of the
// PathNodes begins in the same GraphNode, and no other PathNode shares
// the key. If the maximal set is empty, returns the next PathNode.
template <typename index_t>
pair<index_t, index_t> PathGraph<index_t>::nextMaximalSet(pair<index_t, index_t> range) {
    if(range.second >= nodes.size()) {
        return pair<index_t, index_t>(0, 0);
    }
    range.first = range.second;
    range.second = range.first + 1;
    if(range.first > 0 && nodes[range.first - 1].key == nodes[range.first].key) {
        return range;
    }

    for(index_t i = range.second; i < nodes.size(); i++) {
        if(nodes[i - 1].key != nodes[i].key) {
            range.second = i;
        }
        if(nodes[i].from != nodes[range.first].from) {
            return range;
        }
    }
    range.second = nodes.size();
    return range;
}

template <typename index_t>
pair<index_t, index_t> PathGraph<index_t>::getEdges(index_t node, bool by_from) {
    if(node >= nodes.size()) {
        cerr << "Error: Trying to get edges " << (by_from ? "from " : "to ") << node << endl;
    }
    if(nodes[node].key.second == 0) {
        return pair<index_t, index_t>(0, 0);
    }
    if(node == 0) {
        return pair<index_t, index_t>(0, nodes[node].key.second);
    } else {
        return pair<index_t, index_t>(nodes[node - 1].key.second, nodes[node].key.second);
    }
}
#if 0
template <typename index_t>
void PathGraph<index_t>::sortByFrom(bool create_index) {
    sort(nodes.begin(), nodes.end(), PathNodeFromCmp());
    if(create_index) {
        index_t current = 0;
        nodes.front().key.second = 0;
        for(index_t i = 0; i < nodes.size(); i++) {
            while(current < nodes[i].from) {
                current++;
                assert_lt(current, nodes.size());
                nodes[current].key.second = i;
            }
        }
        for(current++; current < nodes.size(); current++) {
            nodes[current].key.second = nodes.size();
        }
    }
}

template <typename index_t>
pair<index_t, index_t> PathGraph<index_t>::getNodesFrom(index_t node) {
    if(node + 1 >= nodes.size()) {
        assert_eq(node + 1, nodes.size());
        return pair<index_t, index_t>(nodes.back().key.second, nodes.size());
    }
    if(nodes[node].key.second == nodes[node + 1].key.second) {
        return pair<index_t, index_t>(0, 0);
    }
    return pair<index_t, index_t>(nodes[node].key.second, nodes[node + 1].key.second);
}


template <typename index_t>
void PathGraph<index_t>::seperateNodes(void * vp) {
    ThreadParam* threadParam = (ThreadParam*)vp;

    PathGraph<index_t>* previous = threadParam->previous;
    EList<PathNode>& from_nodes = *(threadParam->from_nodes);
    EList<PathNode>& to_nodes = *(threadParam->to_nodes);
    EList<index_t>& from_cur = *(threadParam->from_cur);
    EList<index_t>& to_cur = *(threadParam->to_cur);

    index_t st = threadParam->st;
    index_t en = threadParam->en;
    int partitions = threadParam->partitions;

	for(index_t i = st; i < en; i++) {
		index_t from_hash = hash(previous->nodes[i].from, partitions);
		from_nodes[from_cur[from_hash]] = previous->nodes[i];
		from_cur[from_hash]++;
		if(!previous->nodes[i].isSorted()) {
			index_t to_hash = hash(previous->nodes[i].to, partitions);
			to_nodes[to_cur[to_hash]] = previous->nodes[i];
			to_cur[to_hash]++;
		}
	}
}

template <typename index_t>
void PathGraph<index_t>::seperateNodesCount(void * vp) {
    ThreadParam* threadParam = (ThreadParam*)vp;

    PathGraph<index_t>* previous = threadParam->previous;
    EList<index_t>& from_cur = *(threadParam->from_cur);
    EList<index_t>& to_cur = *(threadParam->to_cur);
    index_t st = threadParam->st;
    index_t en = threadParam->en;
    int partitions = threadParam->partitions;
    for(index_t i = st; i < en; i++) {
		index_t from_hash = hash(previous->nodes[i].from, partitions);
		from_cur[from_hash]++;
		if(!previous->nodes[i].isSorted()) {
			index_t to_hash = hash(previous->nodes[i].to, partitions);
			to_cur[to_hash]++;
		}
	}
}

template <typename index_t>
void PathGraph<index_t>::createCombined(void * vp) {
	ThreadParam2* threadParam = (ThreadParam2*)vp;

	index_t st     = threadParam->st;
	index_t en     = threadParam->en;
    int thread_id  = threadParam->thread_id;
    int nthreads   = threadParam->nthreads;
    int generation = threadParam->generation;

    EList<PathNode>& from_nodes = *(threadParam->from_nodes);
    EList<PathNode>& to_nodes   = *(threadParam->to_nodes);
    PathNode** new_nodes  = threadParam->new_nodes;

    const EList<index_t>& from_size = *(threadParam->from_size);
    const EList<index_t>& to_size   = *(threadParam->to_size);
    const EList<index_t>& from_off = *(threadParam->from_off);
    const EList<index_t>& to_off = *(threadParam->to_off);
    EList<index_t>& new_cur = *(threadParam->new_cur);

    //count
    //use heuristic sometimes instead of counting exact number?
    //other method for small number of to_nodes[i]?
    for(index_t i = st; i < en; i++) {
        if(to_size[i] <= 0) continue;
        assert_gt(from_size[i], 0);
        if(from_size[i] > 1) {
            PathNode* node = from_nodes.begin() + from_off[i];
            sort(node, node + from_size[i], PathNodeFromCmp());
        }
        if(to_size[i] > 1) {
            PathNode* node = to_nodes.begin() + to_off[i];
            sort(node, node + to_size[i], PathNodeToCmp());
        }
		index_t t = 0;
		for(index_t f = 0; f < from_size[i]; f++) {
			while(t < to_size[i] && from_nodes[from_off[i] + f].from > to_nodes[to_off[i] + t].to) {
				t++;
			}
            if(t >= to_size[i]) break;
            // assert_eq(from_nodes[i][f].from, to_nodes[i][t].to);
			index_t shift = 0;
			while(t + shift < to_size[i] && from_nodes[from_off[i] + f].from == to_nodes[to_off[i] + t + shift].to) {
				new_cur[thread_id]++;
				shift++;
			}
		}
    }

    if(new_cur[thread_id] <= 0) {
        return;
    }

    //allocate
    new_nodes[thread_id] = new PathNode[new_cur[thread_id]];
    ASSERT_ONLY(index_t new_cur_count = new_cur[thread_id]);
    new_cur[thread_id] = 0;
    //add
    for(index_t i = st; i < en; i++) {
    	if(from_size[i] <= 0 || to_size[i] <= 0) continue;
    	index_t t = 0;
    	for(index_t f = 0; f < from_size[i]; f++) {
        	while(t < to_size[i] && from_nodes[from_off[i] + f].from > to_nodes[to_off[i] + t].to) {
        		t++;
        	}
            if(t >= to_size[i]) break;
            // assert_eq(from_nodes[i][f].from, to_nodes[i][t].to);
        	index_t shift = 0;
        	while(t + shift < to_size[i] && from_nodes[from_off[i] + f].from == to_nodes[to_off[i] + t + shift].to) {
        		new_nodes[thread_id][new_cur[thread_id]].from = to_nodes[to_off[i] + t + shift].from;
        		new_nodes[thread_id][new_cur[thread_id]].to   = from_nodes[from_off[i] + f].to;
                if(generation < 4) {
                    assert_gt(generation, 0);
                    index_t bit_shift = 1 << (generation - 1);
                    bit_shift = (bit_shift << 1) + bit_shift; // Multiply by 3
                    new_nodes[thread_id][new_cur[thread_id]].key  = pair<index_t, index_t>((to_nodes[to_off[i] + t + shift].key.first << bit_shift) + from_nodes[from_off[i] + f].key.first, 0);
                } else {
                    new_nodes[thread_id][new_cur[thread_id]].key  = pair<index_t, index_t>(to_nodes[to_off[i] + t + shift].key.first, from_nodes[from_off[i] + f].key.first);
                }
        		new_cur[thread_id]++;
        		shift++;
        	}
    	}
    }
    assert_eq(new_cur_count, new_cur[thread_id]);

    if(generation > 3) {
        EList<index_t>&  breakvalues = *(threadParam->breakvalues);
        ELList<index_t>& breakpoints = *(threadParam->breakpoints);

    	sort(new_nodes[thread_id], new_nodes[thread_id] + new_cur[thread_id]);
        assert_lt(thread_id, breakpoints.size());
        breakpoints[thread_id].fillZero();
        index_t low = 0;
        index_t high = 0;
        for(int i = 1; i < nthreads; i++) {
        	low = high;
        	high = new_cur[thread_id];
        	while(high > low) {
        		index_t mid = (low + high) >> 1;
        		if(new_nodes[thread_id][mid].key.first < breakvalues[i]) {
        			low = mid + 1;
        		} else {
        			high = mid;
        		}
        	}
        	breakpoints[thread_id][i] = high;
        }
    	breakpoints[thread_id][nthreads] = new_cur[thread_id];
    }
}

template <typename index_t>
void PathGraph<index_t>::mergeNodes(void * vp) {
	ThreadParam3*       threadParam  = (ThreadParam3*)vp;
	int                 thread_id    = threadParam->thread_id;
	int                 nthreads     = threadParam->nthreads;
	PathGraph<index_t>* previous     = threadParam->previous;
	PathNode**          new_nodes    = threadParam->new_nodes;
	ELList<index_t>&    breakpoints  = *(threadParam->breakpoints);
	PathNode**          merged_nodes = threadParam->merged_nodes;
	index_t*            merged_cur   = threadParam->merged_cur;

	index_t count = 0;
    EList<PathNodeSlice> active;
    active.resizeExact(nthreads + 1);
    active.clear();
	for(int j = 0; j < nthreads; j++) {
		count += breakpoints[j][thread_id + 1] - breakpoints[j][thread_id];
		if(breakpoints[j][thread_id + 1] - breakpoints[j][thread_id]) {
			active.expand();
			active.back().place = new_nodes[j] + breakpoints[j][thread_id];
			active.back().end = new_nodes[j] + breakpoints[j][thread_id + 1];
			active.back().just_made = true;
		}
	}
	count += breakpoints[nthreads][thread_id + 1] - breakpoints[nthreads][thread_id];
	if(breakpoints[nthreads][thread_id + 1] - breakpoints[nthreads][thread_id]) {
		active.expand();
		active.back().place = previous->nodes.begin() + breakpoints[nthreads][thread_id];
		active.back().end = previous->nodes.begin() + breakpoints[nthreads][thread_id + 1];
		active.back().just_made = false;
	}
	sort(active.begin(), active.end());
    if(count <= 0) {
        if(thread_id != 0) {
        	threadParam->merge_mutex[thread_id - 1].unlock();
        }
        return;
    }

    merged_nodes[thread_id] = new PathNode[count];

	while(active.size() > 0) {
		int i = 1;
		while(i < active.size() && active[i] < active[i - 1]) {
			swap(active[i], active[i - 1]);
			i++;
		}
		if(active.front().just_made || active.front().place->isSorted()) {
			merged_nodes[thread_id][merged_cur[thread_id]++] = *(active.front().place);
		}
		active.front().place++;
		if(active.front().place == active.front().end) {
			active.front() = active.back();
			active.pop_back();
		}
	}

    assert_leq(merged_cur[thread_id], count);

    // daehwan - for debugging purposes
#if 0
	index_t* mranks = threadParam->mranks;
	index_t rank = 0;
	for(int j = 0; j < nthreads + 1; j++) {
		rank += breakpoints[j][thread_id];
	}
    mergeUpdateRank(merged_nodes, merged_cur, mranks, thread_id, rank);

    // daehwan -> joe: I think the code below might be moved outside this thread function
    //                 to avoid lock/unlock and to make the code clear

	//removes extra sorted node if separated by boundary
	//needs access to first member of next bin so need mutex
    if(thread_id != 0) {
    	threadParam->merge_mutex[thread_id - 1].unlock();
    }
    if(thread_id + 1 != nthreads) {
        PathNode& last = merged_nodes[thread_id][merged_cur[thread_id] - 1];
        index_t tmp_thread_id = thread_id;
        while(tmp_thread_id + 1 < nthreads) {
            if(merged_cur[tmp_thread_id + 1] > 0) break;
            tmp_thread_id++;
        }
        if(tmp_thread_id + 1 < nthreads) {
            threadParam->merge_mutex[tmp_thread_id].lock();
            PathNode& first = merged_nodes[tmp_thread_id + 1][0];

            if(last.isSorted() && first.isSorted() && last.from == first.from) {
                merged_cur[thread_id]--;
            }
        }
    }

#endif
}


//creates a new PathGraph that is 2^(i+1) sorted from one that is 2^i sorted.
//does so by:
//merging unsorted nodes using a hash join
//sorting the newly made nodes
//merging all nodes together
//updating ranks and adding nodes that become sorted to sorted.
template <typename index_t>
PathGraph<index_t>::PathGraph(PathGraph<index_t>& previous) :
nthreads(previous.nthreads), verbose(previous.verbose),
ranks(0), max_label(previous.max_label), max_from(previous.max_from), temp_nodes(0), generation(previous.generation + 1), sorted(false),
report_node_idx(0), report_edge_range(pair<index_t, index_t>(0, 0)), report_M(pair<index_t, index_t>(0, 0)),
report_F_node_idx(0), report_F_location(0)
{
    assert_gt(nthreads, 0);
#ifndef NDEBUG
    debug = previous.debug;
#endif

    assert_neq(previous.nodes.size(), previous.ranks);

    // first count where to start each from value

	EList<index_t> from_index;
	from_index.resizeExact(max_from + 1);
	from_index.fillZero();

	for(PathNode* node = previous.nodes.begin(); node != previous.nodes.end(); node++) {
		from_index[node->from]++;
	}
	index_t tot = from_index[0];
	from_index[0] = 0;
	for(index_t i = 1; i < from_index.size(); i++) {
		tot += from_index[i];
		from_index[i] = tot - from_index[i];
	}
	//initialize and fill direct-access table
	//use from_index to keep track of where to add next
	EList<pair<index_t, index_t> > from_table;
	from_table.resizeExact(previous.nodes.size());
	from_table.fillZero();
	for(PathNode* node = previous.nodes.begin(); node != previous.nodes.end(); node++) {
		from_table[from_index[node->from]++] = pair<index_t, index_t>(node->to, node->key.first);
	}
	//reset from_index
	for(index_t i = from_index.size() - 1; i > 0; i--) {
		from_index[i] = from_index[i - 1];
	}
	from_index[0] = 0;


	// Now query against hash-table

	//count number of nodes
	temp_nodes = 0;
	for(PathNode* node = previous.nodes.begin(); node != previous.nodes.end(); node++) {
		if(node->isSorted()) {
			temp_nodes++;
		} else {
			temp_nodes += from_index[node->to + 1] - from_index[node->to];
		}
	}
    // make new nodes
    nodes.resizeExact(temp_nodes);
    nodes.clear();
	for(PathNode* node = previous.nodes.begin(); node != previous.nodes.end(); node++) {
		if(node->isSorted()) {
			nodes.push_back(*node);
		} else {
			for(index_t j = from_index[node->to]; j < from_index[node->to + 1]; j++) {
				nodes.expand();
				nodes.back().from = node->from;
				nodes.back().to = from_table[j].first;
                if(generation < 4) {
                    assert_gt(generation, 0);
                    index_t bit_shift = 1 << (generation - 1);
                    bit_shift = (bit_shift << 1) + bit_shift; // Multiply by 3
                    nodes.back().key  = pair<index_t, index_t>((node->key.first << bit_shift) + from_table[j].second, 0);
                } else {
                    nodes.back().key  = pair<index_t, index_t>(node->key.first, from_table[j].second);
                }
			}
		}
	}
	// Now make all nodes properly sorted
	if(generation > 4) {
		PathNode* block_start = nodes.begin();
		for(PathNode* curr = nodes.begin() + 1; curr != nodes.end(); curr++) {
			if(curr->key.first != block_start->key.first) {
				if(curr - block_start > 1) sort(block_start, curr);
				block_start = curr;
			}
		}
		if(nodes.end() - block_start > 1) sort(block_start, nodes.end());
		mergeUpdateRank();
	} else if(generation == 4) {
		sort(nodes.begin(), nodes.end());
		mergeUpdateRank();
	}
}




template <typename index_t>
void PathGraph<index_t>::generateEdgesWorker(void * vp) {
	ThreadParam4* threadParams = (ThreadParam4*)vp;
	int thread_id = threadParams->thread_id;
	int st = threadParams->st;
	int en = threadParams->en;
	RefGraph<index_t>* base = threadParams->base;
	EList<PathNode>* sep_nodes = threadParams->sep_nodes;
	EList<TempEdge>* sep_edges = threadParams->sep_edges;
	EList<PathEdge>* new_edges = threadParams->new_edges;

	for(int i = st; i < en; i++) {

		sort(sep_nodes[i].begin(), sep_nodes[i].end(), PathNodeFromCmp());
		sort(sep_edges[i].begin(), sep_edges[i].end(), TempEdgeToCmp());

		pair<index_t, index_t> pn_range = getNextRange(&sep_nodes[i], pair<index_t, index_t>(0, 0));
		pair<index_t, index_t> ge_range = getNextEdgeRange(&sep_edges[i], pair<index_t, index_t>(0, 0), false /* from? */);

		while(pn_range.first < pn_range.second && ge_range.first < ge_range.second) {
			if(sep_nodes[i][pn_range.first].from == sep_edges[i][ge_range.first].to) {
				for(index_t node = pn_range.first; node < pn_range.second; node++) {
					for(index_t edge = ge_range.first; edge < ge_range.second; edge++) {
						index_t from = sep_edges[i][edge].from;
						new_edges[thread_id].push_back(PathEdge(from, sep_nodes[i][node].key.first, base->nodes[from].label));
					}
				}
				pn_range = getNextRange(&sep_nodes[i], pn_range);
				ge_range = getNextEdgeRange(&sep_edges[i], ge_range, false);
			} else if(sep_nodes[i][pn_range.first].from < sep_edges[i][ge_range.first].to) {
				pn_range = getNextRange(&sep_nodes[i], pn_range);
			} else {
				ge_range = getNextEdgeRange(&sep_edges[i], ge_range, false);
			}
		}
	}
	sort(new_edges[thread_id].begin(), new_edges[thread_id].end());
}
#endif


#endif /*GBWT_GRAPH_H_*/
