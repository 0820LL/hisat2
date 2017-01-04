#!/usr/bin/env python

import sys
import math, random
from datetime import datetime, date, time
from collections import deque
from copy import deepcopy


#
def get_major_nt(nt_dic):
    nt = ''
    max_count = 0
    for tmp_nt, tmp_value in nt_dic.items():
        tmp_count, tmp_var_id = tmp_value
        if len(tmp_nt) == 1:
            assert tmp_nt in "ACGTDN"
        else:
            assert len(tmp_nt) == 2 and tmp_nt[0] == 'I' and tmp_nt[1] in "ACGT"
        if tmp_count > max_count:
            max_count = tmp_count
            nt = tmp_nt
    if len(nt) == 1:
        assert nt in "ACGTDN"
    else:
        assert len(nt) == 2 and nt[0] == 'I' and nt[1] in "ACGT"
    return nt                


#
def match_score(nt_dic1, nt_dic2):
    sum_1 = sum([count for count, _ in nt_dic1.values()])
    sum_2 = sum([count for count, _ in nt_dic2.values()])
    total1, total2 = sum_1 * 2.0, sum_2 * 2.0
    best = 0.0
    for nt in "ACGT":
        if nt not in nt_dic1 or nt not in nt_dic2:
            continue
        tmp_best = nt_dic1[nt][0] / total1 + nt_dic2[nt][0] / total2
        if tmp_best > best:
            best = tmp_best
    return best


#
def get_ungapped_seq(seq):
    ungapped_seq = []
    for i in range(len(seq)):
        nt_dic = seq[i]
        nt = get_major_nt(nt_dic)
        if nt == 'D':
            continue
        ungapped_seq.append(nt_dic)
    return ungapped_seq


#
def get_ungapped_seq_pos(seq, pos):
    tot_del_len, tot_ins_len = 0, 0
    for i in range(len(seq)):
        nt_dic = seq[i]
        nt = get_major_nt(nt_dic)
        if nt == 'D':
            tot_del_len += 1
        elif nt[0] == 'I':
            tot_ins_len += 1
        if i - tot_ins_len == pos:
            return pos - tot_del_len
    return -1


# Get mate node id
#  HSQ1008:141:D0CC8ACXX:3:2304:4780:36964|L to HSQ1008:141:D0CC8ACXX:3:2304:4780:36964|R or vice versa
def get_mate_node_id(node_id):
    node_id2, end = node_id.split('|')
    if end == 'L':
        end = 'R'
    else:
        end = 'L'
    node_id2 = '|'.join([node_id2, end])
    return node_id2



class Node:
    # Initialize
    def __init__(self,
                 id,
                 left,
                 seq,
                 qual,
                 var,
                 ref_seq,
                 ref_vars,
                 mpileup,
                 simulation):
        self.next = [] # list of next nodes

        if simulation:
            id = id.split('_')[0]
        self.id = id # Node ID
        self.left = left # starting position

        # sequence that node represents
        #   with information about how the sequence is related to backbone
        assert len(seq) == len(var)
        assert len(seq) == len(qual)
        self.seq = []
        self.ins_len = 0
        for s in range(len(seq)):
            nt = seq[s]
            if len(nt) == 1:
                assert nt in "ACGTDN"
            else:
                assert len(nt) == 2 and nt[0] == 'I' and nt[1] in "ACGT"
                self.ins_len += 1                
            var_id = var[s]
            self.seq.append({nt : [1, var_id]})
        self.qual = []
        for q in qual:
            if q != '':
                self.qual.append(max(0, ord(q) / 10 - 3))
            else:
                self.qual.append(0)

        self.right = self.left + len(seq) - 1 - self.ins_len

        self.read_ids = set([id])
        self.mate_ids = set([id.split('|')[0]])

        self.calculate_avg_cov()

        self.ref_seq = ref_seq
        self.ref_vars = ref_vars

        self.mpileup = mpileup

        
    # Check how compatible allele is in regard to read or pair
    def compatible_with_rnode(self, rnode):
        assert False
        assert rnode.left + len(rnode.seq) <= len(self.seq)
        score = 0
        for i in range(len(rnode.seq)):
            allele_bp = self.seq[rnode.left + i]
            read_bp = rnode.seq[i]
            if allele_bp == read_bp:
                score += 1

        return float(score) / len(rnode.seq)


    # Check how nodes overlap with each other without considering deletions
    def overlap_with(self, other, vars, skipN = False, debug = False):
        assert self.left <= other.left
        if self.right < other.left:
            return -1, -1
        seq = get_ungapped_seq(self.seq)
        other_seq = get_ungapped_seq(other.seq)
        add_mm = len(self.mate_ids & other.mate_ids)
        i_left = get_ungapped_seq_pos(self.seq, other.left - self.left)
        for i in range(i_left - 5, i_left + 6):
            max_mm = 0.012 * (len(seq) - i) # 1 mismatch per 83 bases
            tmp_mm = 0.0
            for j in range(len(other_seq)):
                if i + j >= len(seq):
                    break
                nt_dic, other_nt_dic = seq[i+j], other_seq[j]
                nt, other_nt = get_major_nt(nt_dic), get_major_nt(other_nt_dic)
                mismatch = 0.0
                if skipN and (nt == 'N' or other_nt == 'N'):
                    mismatch = 0.0
                elif nt != other_nt:
                    mismatch = 1.0 - match_score(seq[i+j], other_seq[j])
                    
                    # Higher penalty for mismatches in variants
                    nt_var, other_nt_var = nt_dic[nt][1], other_nt_dic[other_nt][1]
                    if nt_var != other_nt_var:
                        mismatch = 5.0
                        adjust = min(1.0, nt_dic[nt][0] / self.get_avg_cov()) * \
                                 min(1.0, other_nt_dic[other_nt][0] / other.get_avg_cov())
                        mismatch *= adjust
                        if mismatch < 1.0:
                            mismatch = 1.0

                assert mismatch >= 0.0
                tmp_mm += mismatch
                if tmp_mm > max_mm:
                    break

            if debug:
                print "at %d (%d) with overlap of %d and mismatch of %.2f" % (i, self.left + i, j, tmp_mm)

            if tmp_mm <= max_mm:
                return i, len(seq) - i
                
        return -1, -1

    
    # Combine two nodes with considering deletions
    def combine_with(self, other):
        assert self.left <= other.left

        # Merge two sequences
        assert len(other.seq) > 0 and 'D' not in other.seq[0].keys()
        j = 0        
        # Merge the overlapped parts
        if self.right >= other.left:
            overlap, ins_len = False, 0
            for i in range(len(self.seq)):
                nt_dic = self.seq[i]
                nt = get_major_nt(nt_dic)
                if nt.startswith('I'):
                    ins_len += 1
                if i == other.left - self.left + ins_len:
                    overlap = True
                    break
            assert overlap
            new_seq = self.seq[:i]
            while i < len(self.seq) and j < len(other.seq):
                nt_dic, nt_dic2 = self.seq[i], other.seq[j]
                for nt, value in nt_dic2.items():
                    count, var_id = value
                    if nt in nt_dic:
                        nt_dic[nt][0] += count
                        # if nt != 'D':
                        #    assert nt_dic[nt][1] == var_id
                    else:
                        nt_dic[nt] = [count, var_id]
                new_seq.append(nt_dic)
                i += 1
                j += 1
            # this node contains the other node
            if i < len(self.seq):
                new_seq += self.seq[i:]
        # Fill in the gap between the two nodes if exists
        else:
            new_seq = self.seq[:]
            sum_1 = sum([count for count, _ in self.seq[-1].values()])
            sum_2 = sum([count for count, _ in other.seq[0].values()])
            flank_cov = (sum_1 + sum_2) / 2.0
            for k in range(other.left - self.right - 1):
                ref_nt_dic = self.mpileup[k + 1 + self.right][1]
                nt_dic = {}
                # Fill in the gap with Ns for now
                if len(ref_nt_dic) == 0 or True:
                    nt_dic = {'N' : [1, ""]}
                else:
                    weight = flank_cov / max(1.0, sum([count for count, _ in ref_nt_dic.values()]))
                    for nt, value in ref_nt_dic.items():
                        count, var_id = value
                        nt_dic[nt] = [count * weight, var_id]
                new_seq.append(nt_dic)

        # Append the rest of the other sequence to it
        if j < len(other.seq):
            new_seq += deepcopy(other.seq[j:])
        self.read_ids |= other.read_ids
        self.mate_ids |= other.mate_ids

        self.seq = new_seq
        self.ins_len = 0
        for i in range(len(self.seq)):
            nt_dic = self.seq[i]
            nt = get_major_nt(nt_dic)
            if nt[0] == 'I':
                self.ins_len += 1
        self.right = self.left + len(self.seq) - 1 - self.ins_len
        
        # Update coverage
        self.calculate_avg_cov()


    # Return the length of the ungapped sequence
    def ungapped_length(self):
        return len(get_ungapped_seq(self.seq))


    # Contains Ns?
    def contain_Ns(self):
        for i in range(len(self.seq)):
            nt_dic = self.seq[i]
            nt = get_major_nt(nt_dic)
            if nt == 'N':
                return True
        return False

    
    # Get variant ids
    def get_var_ids(self, left = 0, right = sys.maxint):
        vars = []
        left = max(left, self.left)
        right = min(right, self.right)
        ins_len = 0
        for pos in range(left, right + 1):
            var_i = pos - self.left + ins_len
            while var_i < len(self.seq):
                nt_dic = self.seq[var_i]
                nt = get_major_nt(nt_dic)
                if nt.startswith('I'):
                    var_i += 1
                    ins_len += 1
                else:
                    break            
            for _, var in nt_dic.values():
                if var == "" or \
                   var == "unknown":
                    continue
                assert var in self.ref_vars
                if len(vars) > 0 and var == vars[-1]:
                    continue
                type, pos, data = self.ref_vars[var]
                if data == nt or (type == "deletion" and nt == 'D'):
                    vars.append(var)

        return vars

    
    # Get variant ids
    #   left, right are absolute coordinates
    def get_vars(self, left = 0, right = sys.maxint):
        vars = []
        left = max(left, self.left)
        right = min(right, self.right)
        skip_pos = -1
        ins_len = 0
        for pos in range(left, right + 1):
            if pos <= skip_pos:
                continue
            var_i = pos - self.left + ins_len
            while var_i < len(self.seq):
                nt_dic = self.seq[var_i]
                nt = get_major_nt(nt_dic)
                if nt.startswith('I'):
                    var_i += 1
                    ins_len += 1
                    var = nt_dic[nt][1]
                    if len(vars) > 0 and var != vars[-1][0]:
                        vars.append([var, pos])
                else:
                    break
            if nt == self.ref_seq[pos]:
                continue
            if nt == 'N':
                vars.append(["gap", pos])
                continue            
            added = False
            for _, var in nt_dic.values():
                if var == "" or \
                   var == "unknown":
                    continue
                if len(vars) > 0 and var == vars[-1][0]:
                    continue
                assert var in self.ref_vars
                type, var_pos, data = self.ref_vars[var]                    
                if data == nt or (type == "deletion" and nt == 'D'):
                    assert pos >= var_pos
                    if type == "deletion" and pos > var_pos:
                        continue                    
                    if type == "deletion":
                        skip_pos = pos + int(data) - 1
                    added = True
                    vars.append([var, pos])
            if not added and "unknown" in [var_id for _, var_id in nt_dic.values()]:
                vars.append(["unknown", pos])

        return vars


    # Get average coverage
    def get_avg_cov(self):
        return self.avg

    
    # Calculate average coverage
    def calculate_avg_cov(self):
        self.avg = 0.0
        for nt_dic in self.seq:
            for count, _ in nt_dic.values():
                self.avg += count
        self.avg /= len(self.seq)
        return self.avg

        
    # Display node information
    def print_info(self, output=sys.stderr):
        seq, var_str = "", ""
        prev_var = ""
        ins_len = 0
        for i in range(len(self.seq)):
            if (self.left + i - ins_len) % 100 == 0:
                seq += ("|%d|" % (self.left + i - ins_len))
            elif (self.left + i - ins_len) % 20 == 0:
                seq += '|'
            nt_dic = self.seq[i]
            nt = get_major_nt(nt_dic)
            if nt[0] == 'I':
                seq += "\033[93m"
            elif nt != self.ref_seq[self.left + i - ins_len]:
                var_id = nt_dic[nt][1]
                if var_id == "unknown" or var_id.startswith("nv"):
                    seq += "\033[91m" # red
                else:
                    seq += "\033[94m" # blue
            if nt[0] == 'I':
                seq += nt[1]
            else:
                seq += nt
            if nt[0] == 'I' or nt != self.ref_seq[self.left + i - ins_len]:
                seq += "\033[00m"

            var = []
            for _, var_id in nt_dic.values():
                if var_id == "":
                    continue
                var.append(var_id)
            var = '-'.join(var)
            if var != "" and var != prev_var:
                var_str += "\t%d: %s %s" % (self.left + i - ins_len, var, str(nt_dic))
            prev_var = var
            if nt[0] == 'I':
                ins_len += 1
        
        print >> output, "Node ID:", self.id
        print >> output, "Pos: [%d, %d], Avg. coverage: %.1f" % (self.left, self.right, self.get_avg_cov())
        print >> output, "\t", seq
        print >> output, "\t", var_str
        print >> output, "mates:", len(self.mate_ids) # sorted(self.mate_ids)
        print >> output, "reads:", len(self.read_ids) # sorted(self.read_ids)
        print >> output

                
class Graph:
    def __init__(self,
                 backbone,
                 gene_vars,
                 exons,
                 partial_allele_ids,
                 true_allele_nodes = {},
                 predicted_allele_nodes = {},
                 display_allele_nodes = {},
                 simulation = False):
        self.backbone = backbone # backbone sequence
        self.gene_vars = gene_vars
        self.exons = exons
        self.partial_allele_ids = partial_allele_ids
        self.true_allele_nodes = true_allele_nodes
        self.predicted_allele_nodes = predicted_allele_nodes
        self.allele_node_order = []
        self.display_allele_nodes = display_allele_nodes
        self.simulation = simulation

        self.nodes = {}
        self.edges = {}
        self.to_node, self.from_node = {}, {}

        self.left_margin = 300
        self.right_margin = 20
        self.top_margin = 20
        self.bottom_margin = 20

        if len(backbone) <= 4000:
            self.scalex = 5
        elif len(backbone) <= 8000:
            self.scalex = 2
        else:
            self.scalex = 1
        self.scaley = 2
        self.width = len(self.backbone) * self.scalex + self.left_margin + self.right_margin
        self.unscaled_height = 6000
        self.height = self.unscaled_height * self.scaley


    # Add node, which is an alignment w.r.t. the reference
    def add_node(self, id, node, simulation = False):
        if simulation:
            id = id.split('_')[0]
        if id in self.nodes:
            print >> sys.stderr, "Warning) multi-mapped read:", id
            # assert False
            return
        assert id not in self.nodes
        self.nodes[id] = node

        
    # Generate edges based on the overlapping information between nodes
    def generate_raw_edges(self, overlap_pct = 0.1, skipN = False):
        assert len(self.nodes) > 0
        nodes = []
        for id, node in self.nodes.items():
            nodes.append([id, node.left, node.right])
        def node_cmp(a, b):
            if a[1] != b[1]:
                return a[1] - b[1]
            else:
                return a[2] - b[2]
        nodes = sorted(nodes, cmp=node_cmp)
        
        self.from_node, self.to_node = {}, {}
        for i in range(len(nodes)):
            id1, left1, right1 = nodes[i]
            node1 = self.nodes[id1]
            for j in range(i + 1, len(nodes)):
                id2, left2, right2 = nodes[j]
                if right1 < left2:
                    break
                node2 = self.nodes[id2]
                at, overlap = node1.overlap_with(node2, self.gene_vars, skipN)
                if overlap < node1.ungapped_length() * overlap_pct and \
                   overlap < node2.ungapped_length() * overlap_pct:
                    continue
                if at < 0 or overlap <= 0:
                    continue
                if id1 not in self.to_node:
                    self.to_node[id1] = [[id2, at]]
                else:
                    self.to_node[id1].append([id2, at])
                if id2 not in self.from_node:
                    self.from_node[id2] = [[id1, -at]]
                else:
                    self.from_node[id2].append([id1, -at])

                    
    # Generate edges based on nodes that are close to one another, but not overlapping
    def generate_jump_edges(self):
        assert len(self.nodes) > 0
        nodes = [[id, node.left, node.right] for id, node in self.nodes.items()]
        def node_cmp(a, b):
            if a[1] != b[1]:
                return a[1] - b[1]
            else:
                return a[2] - b[2]
        add_to_node = {}
        nodes = sorted(nodes, cmp=node_cmp)
        for i in range(len(nodes)):
            id, left, right = nodes[i]
            node = self.nodes[id]

            try_jump_edge = True
            num_to_node = 0
            if id in self.to_node:
                to_nodes = self.to_node[id]
                num_to_node = len(to_nodes)
                if len(to_nodes) >= 2:
                    continue
                for id2, at in to_nodes:
                    node2 = self.nodes[id2]
                    overlap = node.right - node.left - at
                    overlap_pct1 = float(overlap) / (node.right - node.left)
                    overlap_pct2 = float(overlap) / (node2.right - node2.left)
                    if max(overlap_pct1, overlap_pct2) > 0.2:
                        try_jump_edge = False
                        break

            if not try_jump_edge:
                continue

            avoid_nodes = set()
            if id in self.to_node:
                avoid_nodes = set([id2 for id2, _ in self.to_node[id]])
                add_avoid_nodes = set()
                for id2 in avoid_nodes:
                    if id2 not in self.to_node:
                        continue
                    add_avoid_nodes |= set([id3 for id3, _ in self.to_node[id2]])
                avoid_nodes |= add_avoid_nodes

            overlap_node_count = 0
            if id in self.to_node:
                overlap_node_count = len(self.to_node[id])
            for j in range(i + 1, len(nodes)):
                id2, left2, right2 = nodes[j]
                if id2 in avoid_nodes:
                    continue

                # DK - in the middle of working now ...
                if right > left2:
                    node2 = self.nodes[id2]
                    overlap = right - left2
                    overlap_pct1 = float(overlap) / (node.right - node.left)
                    overlap_pct2 = float(overlap) / (node2.right - node2.left)
                    if min(overlap_pct1, overlap_pct2) > 0.8:
                        continue
                    
                    """
                    overlap_node_count += 1
                    if overlap_node_count >= 2:
                        break
                    else:
                        continue
                    """

                if id not in add_to_node:
                    add_to_node[id] = []
                add_to_node[id].append([id2, left2 - left])
                if len(add_to_node[id]) + num_to_node >= 2:
                    break
                avoid_nodes.add(id2)
                if id2 in self.to_node:
                    avoid_nodes |= set([id3 for id3, _ in self.to_node[id2]])

        # DK - debugging purposes
        # print add_to_node
        # sys.exit(1)

        for id, to_nodes in add_to_node.items():
            for id2, at in to_nodes:
                if id not in self.to_node:
                    self.to_node[id] = []
                self.to_node[id].append([id2, at])
                if id2 not in self.from_node:
                    self.from_node[id2] = []
                self.from_node[id2].append([id, -at])
        

                    
    # Merge and remove nodes inside other nodes, and update edges accordingly
    def merge_inside_nodes(self):
        # Check which nodes are contained within other nodes
        contained_by = {}
        for id1, to_node_ids in self.to_node.items():
            for id2, at in to_node_ids:
                length1, length2 = self.nodes[id1].ungapped_length(), self.nodes[id2].ungapped_length()
                if at == 0:
                    if length1 >= length2:
                        contained_by[id2] = id1
                    else:
                        contained_by[id1] = id2
                else:
                    assert at > 0
                    if length1 >= length2 + at:
                        contained_by[id2] = id1

        contain = {}
        for id, up_id in contained_by.items():
            while up_id in contained_by:
                up_id = contained_by[up_id]
            contained_by[id] = up_id
            if up_id not in contain:
                contain[up_id] = set([id])
            else:
                contain[up_id].add(id)

        for id, inside_ids in contain.items():
            node = self.nodes[id]
            # DK - temporary solution
            if node.contain_Ns():
                continue
            for id2 in inside_ids:
                node2 = self.nodes[id2]
                # DK - temporary solution
                if node2.contain_Ns():
                    continue

                node.combine_with(node2)
                del self.nodes[id2]

        # Remove the edges of nodes contained within other nodes
        tmp_to_node, tmp_from_node = {}, {}
        for id1, to_node_ids in self.to_node.items():
            if id1 in contained_by:
                continue
            for id2, at in to_node_ids:
                if id2 in contained_by:
                    continue

                assert id1 in self.nodes and id2 in self.nodes
                if id1 not in tmp_to_node:
                    tmp_to_node[id1] = [[id2, at]]
                else:
                    tmp_to_node[id1].append([id2, at])
                if id2 not in tmp_from_node:
                    tmp_from_node[id2] = [[id1, -at]]
                else:
                    tmp_from_node[id2].append([id1, -at])

        self.to_node = tmp_to_node
        self.from_node = tmp_from_node

                    
    # Remove redundant edges
    def remove_redundant_edges(self):
        to_node, from_node = {}, {}
        for id1, to_node_ids in self.to_node.items():
            to_node_ids = set([i[0] for i in to_node_ids])
            to_to_node_ids = set()
            for id2 in to_node_ids:
                if id2 not in self.to_node:
                    continue
                to_to_node_ids |= set([i[0] for i in self.to_node[id2]])

            to_node_ids -= to_to_node_ids
            for id2, at in self.to_node[id1]:
                if id2 not in to_node_ids:
                    continue
                if id1 not in to_node:
                    to_node[id1] = [[id2, at]]
                else:
                    to_node[id1].append([id2, at])
                if id2 not in from_node:
                    from_node[id2] = [[id1, -at]]
                else:
                    from_node[id2].append([id1, -at])

        self.to_node = to_node
        self.from_node = from_node

        # DK - debugging purposes
        """
        for id, to_ids in to_node.items():
            if len(to_ids) > 1:
                print >> sys.stderr, "to>", id, to_ids
                print >> sys.stderr, id,; self.nodes[id].print_info(); print >> sys.stderr
                for id2 in to_ids:
                    print >> sys.stderr, id2,; self.nodes[id2].print_info(); print >> sys.stderr
        for id, from_ids in from_node.items():
            if len(from_ids) > 1:
                print >> sys.stderr, "from>", id, from_ids
                print >> sys.stderr, id,; self.nodes[id].print_info(); print >> sys.stderr
                for id2 in from_ids:
                    print >> sys.stderr, id2,; self.nodes[id2].print_info(); print >> sys.stderr
        """

        
    # Generate edges based on the overlapping information between nodes
    def generate_edges(self, overlap_pct = 0.5, jump_edges = False, skipN = False):
        self.generate_raw_edges(overlap_pct, skipN)
        self.merge_inside_nodes()
        if jump_edges:
            self.generate_jump_edges()
        self.remove_redundant_edges()


    # Remove nodes with relatively low coverage
    def remove_low_cov_nodes(self):
        delete_ids = set()
        nodes = [[id, node.left, node.right] for id, node in self.nodes.items()]
        def node_cmp(a, b):
            if a[2] != b[2]:
                return a[2] - b[2]
            else:
                return a[1] - b[1]
        nodes = sorted(nodes, cmp=node_cmp)
        for n in range(len(nodes)):
            id, left, right = nodes[n]
            node = self.nodes[id]
            i = n - 1
            while i >= 0:
                id2, left2, right2 = nodes[i]
                if right2 < left:
                    break
                node2 = self.nodes[id2]
                left_, right_ = max(left, left2), min(right, right2)
                overlap = right_ - left_
                if overlap < 10:
                    i -= 1
                    continue
                if id not in delete_ids and node.get_avg_cov() < 3.0:
                    overlap_pct = float(overlap) / (right - left)
                    assert overlap_pct <= 1.0
                    if overlap_pct >= 0.5 and \
                       node.get_avg_cov() * (1.3 - overlap_pct) * 10 < node2.get_avg_cov():
                        delete_ids.add(id)
                        
                if id2 not in delete_ids and node2.get_avg_cov() < 3.0:
                    overlap_pct = float(overlap) / (right2 - left2)
                    assert overlap_pct <= 1.0
                    if overlap_pct >= 0.5 and \
                       node2.get_avg_cov() * (1.3 - overlap_pct) * 10 < node.get_avg_cov():
                        delete_ids.add(id2)

                i -= 1

        for delete_id in delete_ids:
            del self.nodes[delete_id]
            
        
    # Display graph information
    def print_info(self): 
        print >> sys.stderr, "Backbone len: %d" % len(self.backbone)
        print >> sys.stderr, "\t%s" % self.backbone   


    # Reduce graph
    def reduce(self, overlap_pct = 0.1):
        to_node = self.to_node
        from_node = self.from_node

        # Assemble unitigs
        unitigs = []
        for id in self.nodes.keys():
            if id in from_node:
                from_ids = [i[0] for i in from_node[id]]
                assert len(from_ids) >= 1
                if len(from_ids) == 1:
                    from_id = from_ids[0]
                    if len(to_node[from_id]) == 1:
                        continue
            
            unitigs.append([id])
            while True:
                if id not in to_node:
                    break
                to_ids = [i[0] for i in to_node[id]]
                if len(to_ids) > 1:
                    break
                to_id = to_ids[0]
                if len(from_node[to_id]) > 1:
                    break
                id = to_id
                unitigs[-1].append(id)

        # Incorporate the nodes that are previously inside or identical to other nodes
        new_unitigs = []
        for unitig in unitigs:
            new_unitig = []
            for id in unitig:
                new_unitig.append(id)
            new_unitigs.append(new_unitig)
        unitigs = new_unitigs

        # Perform the assembly of unitigs into new nodes
        new_nodes = {}
        for unitig in unitigs:
            assert len(unitig) > 0
            id = unitig[0]
            node = self.nodes[id]
            for id2 in unitig[1:]:
                node2 = self.nodes[id2]
                node.combine_with(node2)
            new_nodes[id] = node

        self.nodes = new_nodes
        self.remove_low_cov_nodes()
        self.generate_edges(overlap_pct,
                            True) # jump edge

        # DK - debugging purposes
        # """
        nodes = [[id, node.left, node.right] for id, node in self.nodes.items()]
        def node_cmp(a, b):
            return a[1] - b[1]
        nodes = sorted(nodes, cmp=node_cmp)
        for id, _, _ in nodes:
            print >> sys.stderr, id, "==>", self.to_node[id] if id in self.to_node else []
            self.nodes[id].print_info(); print >> sys.stderr
        # sys.exit(1)
        # """

        
    def informed_assemble(self, params = {"mate": True}):
        mate = "mate" in params and params["mate"]
        allele_nodes = params["alleles"] if "alleles" in params else {}
        vars = self.gene_vars

        # Duplicate nodes when necessary
        iter = 0
        while True and iter < 10:
            iter += 1
            to_node = self.to_node
            from_node = self.from_node
            nodes, new_nodes = self.nodes, {}

            if not mate: # w.r.t. known alleles
                for node in nodes.values():
                    node_vars = node.get_var_ids()
                    max_alleles, max_common = set(), -sys.maxint
                    for anode in allele_nodes.values():
                        allele_vars = anode.get_var_ids(node.left, node.right)
                        tmp_common = len(set(node_vars) & set(allele_vars)) - len(set(node_vars) | set(allele_vars))
                        if tmp_common > max_common:
                            max_common = tmp_common
                            max_alleles = set([anode.id])
                        elif tmp_common == max_common:
                            max_alleles.add(anode.id)

                    node.max_alleles = max_alleles
            
            sorted_nodes = [[id, node.left, node.right] for id, node in nodes.items()]
            def node_cmp(a, b):
                if a[1] != b[1]:
                    return a[1] - b[1]
                else:
                    return a[2] - b[2]
            sorted_nodes = sorted(sorted_nodes, cmp=node_cmp)

            # Resolve two cases iteratively:
            #   (1) one node with two "to" nodes
            #   (2) two nodes with two "to" nodes from them
            matches_list = []
            for id, _, _ in sorted_nodes:
                if id not in to_node:
                    continue                
                to_ids = [i[0] for i in to_node[id]]

                # id has one or two successors
                if len(to_ids) > 2:
                    continue
                matches = []
                from_ids = []
                for to_id in to_ids:
                    if to_id not in from_node:
                        continue
                    for from_id, _ in from_node[to_id]:
                        if from_id not in from_ids:
                            from_ids.append(from_id)
                  
                # The two successors have one or two predecessors in total
                assert len(from_ids) > 0
                if len(from_ids) > 2:
                    continue

                # Special case for one allele simulation
                if len(self.true_allele_nodes) == 1:
                    if len(from_ids) == 1 and len(to_ids) == 1:
                        matches.append([from_ids[0], to_ids[0], 0])
                        matches_list.append(matches)
                        continue
                    
                if len(to_ids) > 2:
                    continue
                if len(from_ids) == 1 and len(to_ids) == 1:
                    continue

                # Make sure "from" nodes precede "to" nodes
                precede = True
                for from_id in from_ids:
                    for to_id in to_ids:
                        if self.nodes[from_id].left >= self.nodes[to_id].left:
                            precede = False
                            break
                if not precede:
                    continue

                mates = []                    
                for i_ in range(len(from_ids)):
                    from_id = from_ids[i_]
                    node1 = nodes[from_id]
                    mates.append([0] * len(to_ids))
                    for j_ in range(len(to_ids)):
                        to_id = to_ids[j_]
                        to_ids2 = [i[0] for i in to_node[from_id]] if from_id in to_node else []
                        if to_id not in to_ids2:
                            continue
                        node2 = nodes[to_id]
                        if mate:
                            mates[i_][j_] = len(node1.mate_ids & node2.mate_ids)
                        else:
                            mates[i_][j_] = len(node1.max_alleles & node2.max_alleles)

                # DK - debugging purposes
                """
                if "479|L" in to_ids and \
                   "164|R" in to_ids:
                    print from_ids, "===>", to_ids
                    print mates
                    sys.exit(1)
                """

                mult = 2 if mate else 1
                if len(from_ids) == 1 and len(to_ids) == 2:
                    if from_ids[0] == sorted_nodes[0][0]:
                        if mates[0][0] > mates[0][1] * mult:
                            matches.append([from_ids[0], to_ids[0], mates[0][0]])
                        elif mates[0][0] * mult < mates[0][1]:
                            matches.append([from_ids[0], to_ids[1], mates[0][1]])
                        else:
                            matches.append([from_ids[0], to_ids[0], mates[0][0]])
                            matches.append([from_ids[0], to_ids[1], mates[0][1]])                            
                    else:
                        for to_id in to_ids:
                            matches.append([from_ids[0], to_id, 0])
                elif len(from_ids) == 2 and len(to_ids) == 1:
                    if to_ids[0] == sorted_nodes[-1][0]:
                        if mates[0][0] > mates[1][0] * mult:
                            matches.append([from_ids[0], to_ids[0], mates[0][0]])
                        elif mates[0][0] * mult < mates[1][0]:
                            matches.append([from_ids[1], to_ids[0], mates[1][0]])
                        elif mates[0][0] > 0:
                            matches.append([from_ids[0], to_ids[0], mates[0][0]])
                            matches.append([from_ids[1], to_ids[0], mates[1][0]])
                else:
                    assert len(from_ids) == 2 and len(to_ids) == 2
                    score00 = mates[0][0] + mates[1][1]
                    score01 = mates[0][1] + mates[1][0]
                    if (mate and score00 > max(2, score01 * mult)) or \
                       (not mate and score00 > score01):
                        matches.append([from_ids[0], to_ids[0], mates[0][0]])
                        matches.append([from_ids[1], to_ids[1], mates[1][1]])
                    elif (mate and score01 > max(2, score00 * mult)) or \
                         (not mate and score01 > score00):
                        matches.append([from_ids[0], to_ids[1], mates[0][1]])
                        matches.append([from_ids[1], to_ids[0], mates[1][0]])

                    if len(matches) != 2:
                        continue

                if len(matches) <= 0:
                    continue
                matches_list.append(matches)

                # DK - debugging purposes
                """
                debug_id = "HSQ1009:116:C0J32ACXX:3:1204:7306:26074|R"
                if debug_id in from_ids or debug_id in to_ids or True:
                    print >> sys.stderr, "to:", id, "has", to_ids
                    print >> sys.stderr, "from:", id, "has", from_ids
                    print >> sys.stderr, matches
                    for from_id, id, _ in matches:
                        # nodes[from_id].print_info(sys.stderr)
                        # nodes[id].print_info(sys.stderr)
                        print >> sys.stderr, from_id, nodes[from_id].left, nodes[from_id].right
                        print >> sys.stderr, id, nodes[id].left, nodes[id].right
                        print >> sys.stderr, "mates:", len(nodes[from_id].mate_ids), "vs.", len(nodes[id].mate_ids)
                        print >> sys.stderr, "mates common", len(nodes[from_id].mate_ids & nodes[id].mate_ids)
                    print >> sys.stderr, "mate count:", mates
                    # for from_id in from_ids:
                    #     nodes[from_id].print_info(sys.stderr)
                    #     print >> sys.stderr, "mates:", len(nodes[from_id].mate_ids)
                    # print >> sys.stderr, "mates common between from nodes", len(nodes[from_ids[0]].mate_ids & nodes[from_ids[1]].mate_ids)
                    print >> sys.stderr, "Iter:", iter
                    sys.exit(1)
                """

            delete_nodes = set()
            for matches in matches_list:
                for from_id, id, _ in matches:
                    sep = '-' if mate else '+'
                    sep = sep * iter
                    new_id = from_id + sep + id
                    if new_id in new_nodes:
                        continue
                    from_node, node = deepcopy(nodes[from_id]), nodes[id]; delete_nodes.add(from_id)
                    from_node.id = new_id                        
                    
                    from_node.combine_with(node); delete_nodes.add(id)
                    new_nodes[new_id] = from_node

            for id, node in nodes.items():
                if id in delete_nodes or id in new_nodes:
                    continue
                new_nodes[id] = node

            self.nodes = new_nodes
            self.remove_low_cov_nodes()
            self.generate_edges(0.02,
                                True, # jump_edge
                                True) # skipN
            self.reduce(0.02)

            if len(matches_list) <= 0:
                break

            # DK - debugging purposes
            # if iter >= 1:
            #    break

        # DK - debugging purposes
        # """
        print >> sys.stderr, "Iter:", iter
        nodes = [[id, node.left, node.right] for id, node in self.nodes.items()]
        def node_cmp(a, b):
            return a[1] - b[1]
        nodes = sorted(nodes, cmp=node_cmp)
        for id, _, _ in nodes:
            print >> sys.stderr, id, "==>", self.to_node[id] if id in self.to_node else []
            self.nodes[id].print_info(); print >> sys.stderr
        # sys.exit(1)
        # """

        
    # Reduce the graph using mate pairs
    def assemble_with_mates(self):
        self.informed_assemble({"mate" : True})

            
    # Assemble by aligning to known alleles
    def assemble_with_alleles(self):
        self.informed_assemble({"allele" : True, "alleles" : self.predicted_allele_nodes})


    # Compare nodes and get information
    def get_node_comparison_info(self, node_dic):
        assert len(node_dic) > 0
        nodes = [[id, node.left, node.right] for id, node in node_dic.items()]
        def node_cmp(a, b):
            if a[1] != b[1]:
                return a[1] - b[1]
            else:
                return a[2] - b[2]
        nodes = sorted(nodes, cmp=node_cmp)
        seqs, colors = [], []
        for p in range(len(self.backbone)):
            nts = set()
            for n in range(len(nodes)):
                id, left, right = nodes[n]
                node = node_dic[id]
                if p >= left and p <= right:
                    nt_dic = node.seq[p - left]
                    nt = get_major_nt(nt_dic)
                    nts.add(nt)

            for n in range(len(nodes)):
                if p == 0:
                    seqs.append([])
                    colors.append([])
                id, left, right = nodes[n]
                node = node_dic[id]
                if p >= left and p <= right:
                    nt_dic = node.seq[p - left]
                    nt = get_major_nt(nt_dic)
                    seqs[n].append(nt)
                    if nt != self.backbone[p]:
                        if len(nts) > 1:
                            colors[n].append('R')
                        else:
                            colors[n].append('B')
                    else:
                        colors[n].append('N')
                else:
                    seqs[n].append(' ')

        assert len(nodes) == len(seqs)
        for n in range(len(nodes)):
            node, seq, color = nodes[n], seqs[n], colors[n]
            new_left, new_right = 0, len(seq) - 1
            while seq[new_left] == 'D':
                new_left += 1
            while seq[new_right] == 'D':
                new_right -= 1

            node[1] = new_left
            node[2] = new_right
            seqs[n] = seq[new_left:new_right+1]
            colors[n] = color[new_left:new_right+1]

        return nodes, seqs, colors


    # Compare nodes
    def print_node_comparison(self, node_dic):
        nodes, seqs, colors = self.get_node_comparison_info(node_dic)
        interval = 100
        for p in range(0, (len(self.backbone) + interval - 1) / interval * interval, interval):
            cur_seqs = []
            for n in range(len(nodes)):
                id, left, right = nodes[n] # inclusive coordinate
                right += 1
                seq = []
                seq_left, seq_right = max(p, left), min(p+interval, right)
                if seq_left >= seq_right:
                    continue
                if p < left:
                    seq += ([' '] * (left - p))
                for s in range(seq_left, seq_right):
                    nt, color = seqs[n][s-left], colors[n][s-left]
                    if color in "RB":
                        if color == 'R':
                            nt = "\033[91m" + nt
                        else:
                            nt = "\033[94m" + nt
                        nt += "\033[00m"        
                    seq.append(nt)
                if right < p + interval:
                    seq += ([' '] * (p + interval - right))
                seq = ''.join(seq)
                cur_seqs.append([seq, id])

            if len(cur_seqs) <= 0:
                continue
                
            print >> sys.stderr, p
            for seq, id in cur_seqs:
                print >> sys.stderr, "\t", seq, id
                                
        
    # Begin drawing graph
    def begin_draw(self, fname_base):
        assert len(self.nodes) > 0
        nodes = [[id, node.left, node.right] for id, node in self.nodes.items()]
        def node_cmp(a, b):
            return a[1] - b[1]
        nodes = sorted(nodes, cmp=node_cmp)

        def get_x(x):
            return self.left_margin + x * self.scalex

        def get_y(y):
            return self.top_margin + y * self.scaley

        # Get scalar
        def get_sx(x):
            return x * self.scalex

        def get_sy(y):
            return y * self.scaley

        htmlDraw = self.htmlDraw = HtmlDraw(fname_base)
        htmlDraw.write_html_css(self.width, self.height)
        htmlDraw.start_js()
        # htmlDraw.draw_smile()
        js_file = htmlDraw.js_file

        # Choose font
        print >> js_file, r'ctx.font = "12px Times New Roman";'

        # Draw vertical dotted lines at every 100nt and thick lines at every 500nt
        print >> js_file, r'ctx.fillStyle = "gray";'
        for pos in range(0, nodes[-1][2], 100):
            if pos != 0 and pos % 500 == 0:
                print >> js_file, r'ctx.setLineDash([]);'
                print >> js_file, r'ctx.lineWidth = 1;'
            else:
                print >> js_file, r'ctx.setLineDash([5, 15]);'
                print >> js_file, r'ctx.lineWidth = 0.2;'

            print >> js_file, r'ctx.beginPath();'
            print >> js_file, r'ctx.moveTo(%d, %d);' % \
                (get_x(pos), self.top_margin)
            print >> js_file, r'ctx.lineTo(%d, %d);' % \
                (get_x(pos), self.height)
            print >> js_file, r'ctx.stroke();'

        print >> js_file, r'ctx.setLineDash([]);'


    # End drawing graph
    def end_draw(self):
        self.htmlDraw.end_js()

        
    # Draw graph
    #   Top left as (0, 0) and Bottom right as (width, height)
    def draw(self,
             begin_y,
             title = ""):
        assert len(self.nodes) > 0
        nodes = [[id, node.left, node.right] for id, node in self.nodes.items()]
        def node_cmp(a, b):
            return a[1] - b[1]
        nodes = sorted(nodes, cmp=node_cmp)
        max_right = len(self.backbone)

        # display space
        dspace = [[[begin_y, self.unscaled_height]]] * (max_right + 1)
        def get_dspace(left, right, height):
            assert left < len(dspace) and right < len(dspace)
            range1 = dspace[left]
            for range2 in dspace[left + 1:right + 1]:
                new_range = []
                # sub range
                for t1, b1 in range1:
                    for t2, b2 in range2:
                        if b1 < t2:
                            break
                        if b2 < t1:
                            continue
                        t, b = max(t1, t2), min(b1, b2)
                        if b - t >= height:
                            new_range.append([t, b])

                range1 = new_range
            if len(range1) <= 0:
                return 0

            t, b = range1[0]
            assert b - t >= height
            b = t + height
            for i in range(left, right+1):
                range1 = dspace[i]
                range2 = []
                found = False
                for j in range(len(range1)):
                    t2, b2 = range1[j]
                    if t2 <= t and b <= b2:
                        found = True
                        if t2 < t:
                            range2.append([t2, t])
                        if b < b2:
                            range2.append([b, b2])
                    else:
                        range2.append([t2, b2])
                dspace[i] = range2
                assert found
            return t

        def get_x(x):
            return self.left_margin + x * self.scalex

        def get_y(y):
            return self.top_margin + y * self.scaley

        # Get scalar
        def get_sx(x):
            return x * self.scalex

        def get_sy(y):
            return y * self.scaley

        htmlDraw = self.htmlDraw
        # htmlDraw.draw_smile()
        js_file = htmlDraw.js_file

        # Draw exons
        y = get_dspace(0, max_right, 14)
        for e in range(len(self.exons)):
            left, right = self.exons[e]
            right += 1

            # Draw node
            print >> js_file, r'ctx.beginPath();'
            print >> js_file, r'ctx.rect(%d, %d, %d, %d);' % \
                (get_x(left), get_y(y), get_x(right) - get_x(left), get_sy(10))
            print >> js_file, r'ctx.fillStyle = "white";'
            print >> js_file, r'ctx.fill();'
            print >> js_file, r'ctx.lineWidth = 2;'
            print >> js_file, r'ctx.strokeStyle = "black";'
            print >> js_file, r'ctx.stroke();'

            # Draw label
            print >> js_file, r'ctx.fillStyle = "blue";'
            print >> js_file, r'ctx.fillText("Exon %d", %d, %d);' % \
                (e+1, get_x(left + 2), get_y(y + 7))

            if e > 0:
                prev_right = self.exons[e-1][1] + 1
                print >> js_file, r'ctx.beginPath();'
                print >> js_file, r'ctx.moveTo(%d, %d);' % (get_x(left), get_y(y + 5))
                print >> js_file, r'ctx.lineTo(%d, %d);' % (get_x(prev_right), get_y(y + 5))
                print >> js_file, r'ctx.stroke();'

        # Draw true or predicted alleles
        node_colors = ["#FFFF00", "#00FF00", "#FFCBA4", "#C14581"]
        allele_node_colors = ["#DDDD00", "#008800", "#DDA982", "#A12561"]
        def draw_alleles(allele_node_dic, allele_node_colors, display = False):
            if len(allele_node_dic) <= 0:
                return
            allele_nodes, seqs, colors = self.get_node_comparison_info(allele_node_dic)
            for n_ in range(len(allele_nodes)):
                n = -1
                prob = ""
                if not display and \
                   not self.simulation and \
                   len(self.allele_node_order) == len(allele_node_dic):
                    allele_id, prob = self.allele_node_order[n_]
                    for n2_ in range(len(allele_nodes)):
                        if allele_id == allele_nodes[n2_][0]:
                            n = n2_
                            break
                    prob = ": %.2f" % prob
                else:
                    n = n_
                assert n >= 0 and n < len(allele_nodes)
                allele_id, left, right = allele_nodes[n]
                right += 1
                allele_node = allele_node_dic[allele_id]
                y = get_dspace(0, max_right, 14)

                # Draw allele name
                if display:
                    allele_type = "Omixon"
                else:
                    if self.simulation:
                        allele_type = "true"
                    else:
                        allele_type = "predicted"
                print >> js_file, r'ctx.fillStyle = "blue";'
                print >> js_file, r'ctx.font = "20px Times New Roman";'
                print >> js_file, r'ctx.fillText("%s (%s, %s%s)", %d, %d);' % \
                    (allele_id,
                     "partial" if allele_id in self.partial_allele_ids else "full",
                     allele_type,
                     # prob,
                     "",
                     10,
                     get_y(y + 5))
                print >> js_file, r'ctx.font = "12px Times New Roman";'
        
                # Draw node
                print >> js_file, r'ctx.beginPath();'
                print >> js_file, r'ctx.rect(%d, %d, %d, %d);' % \
                    (get_x(left), get_y(y), get_x(right) - get_x(left), get_sy(10))
                print >> js_file, r'ctx.fillStyle = "%s";' % (allele_node_colors[n % len(allele_node_colors)])
                print >> js_file, r'ctx.fill();'
                print >> js_file, r'ctx.lineWidth = 2;'
                print >> js_file, r'ctx.strokeStyle = "black";'
                print >> js_file, r'ctx.stroke();'

                color_boxes = []
                c = 0
                while c < len(colors[n]):
                    color = colors[n][c]
                    c2 = c + 1
                    if color != 'N':                        
                        while c2 < len(colors[n]):
                            color2 = colors[n][c2]
                            if color != color2:
                                break
                            c2 += 1
                        color_boxes.append([c, c2, color])
                    c = c2

                # Draw variants
                for color_box in color_boxes:
                    cleft, cright, color = color_box
                    cleft += left; cright += left
                    if color == 'B':
                        color = "blue" 
                    else:
                        color = "#1E90FF"
                    # DK - debugging purposes
                    color = "blue"
                    print >> js_file, r'ctx.beginPath();'
                    print >> js_file, r'ctx.rect(%d, %d, %d, %d);' % \
                        (get_x(cleft), get_y(y + 1), get_x(cright) - get_x(cleft), get_sy(8))
                    print >> js_file, r'ctx.fillStyle = "%s";' % (color)
                    print >> js_file, r'ctx.fill();'
            return allele_nodes, seqs, colors

        allele_nodes, seqs, colors = draw_alleles(self.true_allele_nodes if self.simulation else self.predicted_allele_nodes,
                                                  allele_node_colors)
        draw_alleles(self.display_allele_nodes,
                     ["#FFF5EE"],
                     True) # display alleles?

        # Draw location at every 100bp
        y = get_dspace(0, nodes[-1][2], 14)
        for pos in range(0, nodes[-1][2], 100):
            # Draw label
            print >> js_file, r'ctx.fillStyle = "blue";'
            print >> js_file, r'ctx.fillText("%d", %d, %d);' % \
                (pos, get_x(pos+2), get_y(y + 2))

        # Draw nodes
        node_to_y = {}
        draw_title = False
        for id, left, right in nodes:
            node = self.nodes[id]

            # Get y position
            y = get_dspace(left, right, 14)
            node_to_y[id] = y

            node_vars = node.get_vars()
            node_var_ids = node.get_var_ids()
            if len(allele_nodes) > 0:
                color = "white"
                max_common = -sys.maxint
                for a in range(len(allele_nodes)):
                    allele_node_id, allele_left, allele_right = allele_nodes[a]
                    if right - left <= 500 and (left < allele_left or right > allele_right):
                        continue
                    if self.simulation:
                        allele_node = self.true_allele_nodes[allele_node_id]
                    else:
                        allele_node = self.predicted_allele_nodes[allele_node_id]
                    allele_vars = allele_node.get_var_ids(left, right)
                    common_vars = set(node_var_ids) & set(allele_vars)
                    tmp_common = len(common_vars) - len(set(node_var_ids) | set(allele_vars))
                    if max_common < tmp_common:
                        max_common = tmp_common
                        color = node_colors[a % len(node_colors)]
                    elif max_common == tmp_common:
                        color = "white"
            else:
                color = "yellow"    

            # Draw node
            right += 1
            print >> js_file, r'ctx.beginPath();'
            print >> js_file, r'ctx.rect(%d, %d, %d, %d);' % \
                (get_x(left), get_y(y), get_x(right) - get_x(left), get_sy(10))
            print >> js_file, r'ctx.fillStyle = "%s";' % color
            print >> js_file, r'ctx.fill();'
            print >> js_file, r'ctx.lineWidth = 2;'
            print >> js_file, r'ctx.strokeStyle = "black";'
            print >> js_file, r'ctx.stroke();'

            # Draw variants
            for var_id, pos in node_vars:
                if var_id == "gap":
                    var_type, var_left = "single", pos
                    color = "black"
                elif var_id == "unknown" or var_id.startswith("nv"):
                    var_type, var_left = "single", pos
                    color = "red"
                else:
                    var_type, var_left, var_data = self.gene_vars[var_id]
                    color = "blue"
                if var_type == "single":
                    var_right = var_left + 1
                else:
                    assert var_type == "deletion"
                    var_right = var_left + int(var_data)
                print >> js_file, r'ctx.beginPath();'
                print >> js_file, r'ctx.rect(%d, %d, %d, %d);' % \
                    (get_x(var_left), get_y(y + 1), get_x(var_right) - get_x(var_left), get_sy(8))
                print >> js_file, r'ctx.fillStyle = "%s";' % (color)
                print >> js_file, r'ctx.fill();'

            # Draw label
            if get_sx(right - left) >= 300:
                print >> js_file, r'ctx.fillStyle = "blue";'
                print >> js_file, r'ctx.fillText("%s", %d, %d);' % \
                    (node.id, get_x(left + 2), get_y(y + 7))

            if not draw_title:
                draw_title = True
                print >> js_file, r'ctx.font = "24px Times New Roman";'
                print >> js_file, r'ctx.fillText("%s", %d, %d);' % \
                    (title, 10, get_y(y + 7))
                print >> js_file, r'ctx.font = "12px Times New Roman";'


        # Draw edges
        print >> js_file, r'ctx.lineWidth = 1;'
        line_colors = ["red", "black", "blue"]
        for node_id, to_node_ids in self.to_node.items():
            node = self.nodes[node_id]
            node_x = (get_x(node.left) + get_x(node.right)) / 2
            node_y = get_y(node_to_y[node_id] + 5)
            print >> js_file, r'ctx.strokeStyle = "%s";' % \
                line_colors[random.randrange(len(line_colors))]
            for to_node_id, _ in to_node_ids:
                to_node = self.nodes[to_node_id]
                to_node_x = (get_x(to_node.left) + get_x(to_node.right) + (random.random() * 10 - 5)) / 2
                to_node_y = get_y(node_to_y[to_node_id] + 5)

                jitter1, jitter2 = (random.random() * 10 - 5), (random.random() * 10 - 5)
                jitter1, jitter2 = get_sx(jitter1), get_sx(jitter2)

                print >> js_file, r'ctx.beginPath();'
                print >> js_file, r'ctx.moveTo(%d, %d);' % (node_x + jitter1, node_y)
                print >> js_file, r'ctx.lineTo(%d, %d);' % (to_node_x + jitter2, to_node_y)
                print >> js_file, r'ctx.stroke();'

        return get_dspace(0, nodes[-1][2], 1)


    #
    # 
    def guided_DeBruijn(self):
        assert len(self.nodes) > 0
        k = 60 # k-mer

        DRB1_debug = False

        node_seq = {}
        for id, node in self.nodes.items():
            s, seq = 0, []
            while s < len(node.seq):
                nt_dic = node.seq[s] # {'C': [1, '']}
                nt = get_major_nt(nt_dic)
                if nt in "ACGTND":
                    seq.append(nt)
                else:
                    assert len(nt) == 2 and nt[0] == 'I' and nt[1] in "ACGT"
                s += 1

            if len(seq) < k:
                continue

            def leftshift(seq, ref_seq):
                seq_len = len(seq)
                assert seq_len > 0 and seq[0] != 'D'

                bp_i = 0
                while bp_i < seq_len:
                    bp = seq[bp_i]
                    if bp != 'D':
                        bp_i += 1
                        continue
                    bp_j = bp_i + 1
                    while bp_j < seq_len:
                        bp2 = seq[bp_j]
                        if bp2 != 'D':
                            break
                        else:
                            bp_j += 1

                    if bp_j >= seq_len:
                        bp_i = bp_j
                        break

                    prev_i, prev_j = bp_i, bp_j
                    while bp_i > 0 and seq[bp_i-1] in "ACGT" and ref_seq[bp_j-1] in "ACGT":
                        if seq[bp_i-1] != ref_seq[bp_j-1]:
                            break
                        seq[bp_j-1] = seq[bp_i-1]
                        seq[bp_i-1] = 'D'
                        bp_i -= 1
                        bp_j -= 1
                    bp_i = bp_j
                    while bp_i < seq_len:
                        if seq[bp_i] in "ACGT":
                            break
                        bp_i += 1

            if DRB1_debug:
                leftshift(seq, self.backbone[node.left:node.left + len(seq)])
            node_seq[id] = seq

        try_hard = False
        while True:
            delete_ids = set()
            nodes = []
            for id, node in self.nodes.items():
                seq = node_seq[id]
                if len(seq) < k:
                    continue
                kmer, seq = seq[:k], seq[k:]
                nodes.append([id, node.left, node.right, kmer, seq])
                
            def node_cmp(a, b):
                if a[1] != b[1]:
                    return a[1] - b[1]
                else:
                    return a[2] - b[2]
            nodes = sorted(nodes, cmp=node_cmp)

            # Generate numerical read IDs
            id_to_num = {}
            num_to_id = []
            for id in [node[0] for node in nodes]:
                id_to_num[id] = len(id_to_num)
                num_to_id.append(id)

            # Construct De Bruijn graph with 80-mer
            self.debruijn = debruijn = [[] for i in range(len(self.backbone) - k + 1)]
            min_n = 0
            for pos in range(len(debruijn)):
                for n in range(min_n, len(nodes)):
                    id, node_pos, node_right, kmer, seq = nodes[n]
                    if node_pos < pos:
                        min_n = n + 1
                        continue
                    elif node_pos > pos:
                        break

                    assert len(kmer) == k

                    # Add a new node or update the De Bruijn graph
                    curr_vertices = debruijn[pos]
                    found = False
                    kmer_seq = ''.join(kmer)
                    for v in range(len(curr_vertices)):
                        cmp_nt, cmp_k_m1_mer = curr_vertices[v][:2]
                        if kmer_seq == cmp_k_m1_mer + cmp_nt:                        
                            curr_vertices[v][3].append(n)
                            found = True
                            break

                    if not found:
                        predecessors = []
                        if pos > 0:
                            prev_vertices = debruijn[pos - 1]
                            for v in range(len(prev_vertices)):
                                cmp_nt, cmp_k_m1_mer = prev_vertices[v][:2]
                                if kmer_seq[:-1] == cmp_k_m1_mer[1:] + cmp_nt:
                                    predecessors.append(v)
                        debruijn[pos].append([kmer_seq[-1],           # base
                                              ''.join(kmer_seq[:-1]), # (k-1)-mer
                                              predecessors,           # predecessors
                                              [n]])                   # numeric read IDs

                    # Update k-mer
                    if len(seq) > 0:
                        kmer, seq = kmer[1:] + seq[:1], seq[1:]
                        nodes[n] = [id, node_pos + 1, node_right, kmer, seq]

            # Average number of kmers
            total_kmers = 0
            for pos in range(len(debruijn)):
                vertices = debruijn[pos]
                for _, _, _, num_ids in vertices:
                    total_kmers += len(num_ids)
            avg_kmers = float(total_kmers) / len(debruijn)

            # Filter out reads
            for pos in range(len(debruijn)):
                vertices = debruijn[pos]
                num_vertices = 0
                num_kmers = 0
                for v in range(len(vertices)):
                    _, _, predecessors, num_ids = vertices[v]
                    if not (set(num_ids) <= delete_ids):
                        num_vertices += 1
                        if DRB1_debug:
                            num_kmers = len(set(num_ids) - delete_ids)
                if num_vertices <= 1:
                    if DRB1_debug:
                        if pos > 300 and pos + 300 < len(debruijn):
                            if num_vertices == 1 and num_kmers * 8 < avg_kmers:
                                for _, _, _, num_ids in vertices:
                                    delete_ids |= set(num_ids)
                    continue
                
                vertice_count = [0] * len(vertices)
                for v in range(len(vertices)):
                    _, _, predecessors, num_ids = vertices[v]
                    for num_id in num_ids:
                        if num_id in delete_ids:
                            continue
                        read_id = num_to_id[num_id]
                        mate_read_id = get_mate_node_id(read_id)
                        if mate_read_id in self.nodes:
                            vertice_count[v] += 1

                # DK - debugging purposes
                debug_msg = False
                if debug_msg:
                    print >> sys.stderr, "at", pos, vertices
                    print >> sys.stderr, "count:", vertice_count

                if try_hard:
                    vertice_with_id = [[vertice_count[v], v] for v in range(len(vertice_count))]
                    vertice_with_id = sorted(vertice_with_id, key=lambda a: a[0])
                    for v in range(len(vertice_count) - 2):
                        v = vertice_with_id[v][1]
                        num_ids = vertices[v][3]
                        delete_ids |= set(num_ids)
                        if debug_msg:
                            print >> sys.stderr, v, "is removed with", num_ids
                else:
                    for v in range(len(vertices)):
                        assert len(vertices) >= 2
                        relative_avg = (sum(vertice_count) - vertice_count[v]) / float(len(vertice_count) - 1)
                        if len(vertices) == 2:
                            # DK - working on ...
                            if DRB1_debug:
                                if vertice_count[v] * 8 < relative_avg:
                                    num_ids = vertices[v][3]
                                    delete_ids |= set(num_ids)
                                    if debug_msg:
                                        print >> sys.stderr, v, "is removed with", num_ids
                                elif vertice_count[v] * 8 < avg_kmers:
                                    num_ids = vertices[v][3]
                                    delete_ids |= set(num_ids)
                                elif vertice_count[v] * 2 < relative_avg:
                                    nt, kmer, _, num_ids = vertices[1-v]
                                    if nt == 'D':
                                        num_id = num_ids[0]
                                        read_id = num_to_id[num_id]
                                        left, seq = pos-self.nodes[read_id].left, node_seq[read_id]

                                        seq_right = ''.join(seq[left+k:])
                                        # print "ref:", ''.join(seq[left:left+k]), seq_right
                                        # print "k  :", kmer, nt
                                        seq_right = seq_right.replace('D', '')
                                        success = True
                                        for num_id2 in vertices[v][3]:
                                            read_id2 = num_to_id[num_id2]
                                            left2, seq2 = pos-self.nodes[read_id2].left, node_seq[read_id2]
                                            seq2_right = ''.join(seq2[left2+k:])
                                            # print "cmp:", ''.join(seq2[left2:left2+k]), seq2_right
                                            # print seq_right.find(seq2_right) == 0
                                            if seq_right.find(seq2_right) != 0:
                                                success = False
                                                break
                                        if success:
                                            delete_ids |= set(vertices[v][3])
                        else:
                            if vertice_count[v] * 3 < relative_avg:
                                num_ids = vertices[v][3]
                                delete_ids |= set(num_ids)
                                if debug_msg:
                                    print >> sys.stderr, v, "is removed with", num_ids

                if debug_msg:
                    print >> sys.stderr
                    print >> sys.stderr             
                
            if len(delete_ids) == 0:
                if try_hard:
                    break
                else:
                    try_hard = True

            for num_id in delete_ids:
                read_id = num_to_id[num_id]
                del self.nodes[read_id]

        # Print De Bruijn graph
        # """
        # for i in range(len(debruijn)):
        for i in range(len(debruijn)):
            curr_vertices = debruijn[i]
            if len(curr_vertices) == 0:
                continue
            consensus_seq = [{} for j in range(k)]
            for v in range(len(curr_vertices)):
                nt, k_m1_mer = curr_vertices[v][:2]
                kmer = k_m1_mer + nt
                assert len(kmer) == k
                for j in range(k):
                    nt = kmer[j]
                    if nt not in consensus_seq[j]:
                        consensus_seq[j][nt] = 1
                    else:
                        consensus_seq[j][nt] += 1

            print >> sys.stderr, i
            for v in range(len(curr_vertices)):
                nt, k_m1_mer, predecessors, num_ids = curr_vertices[v]
                kmer = k_m1_mer + nt
                kmer_seq = ""
                for j in range(k):
                    nt = kmer[j]
                    if len(consensus_seq[j]) >= 2:
                        kmer_seq += "\033[94m"
                    kmer_seq += nt
                    if len(consensus_seq[j]) >= 2:
                        kmer_seq += "\033[00m"
                    
                print >> sys.stderr, "\t%d:" % v, kmer_seq, len(num_ids), predecessors, num_ids
                    

        # """

        # Generate compressed nodes
        paths = []
        path_queue, done = deque(), set()
        for i in range(len(debruijn)):
            if len(debruijn[i]) == 0:
                continue
            for i2 in range(len(debruijn[i])):
                path_queue.append("%d-%d" % (i, i2))
            break

        while len(path_queue) > 0:
            i_str = path_queue.popleft()
            if i_str in done:
                continue

            i, i2 = i_str.split('-')
            i, i2 = int(i), int(i2)
            num_ids = debruijn[i][i2][3]
            j = i + 1
            while j < len(debruijn):
                merge, branch = len(debruijn[j-1]) > len(debruijn[j]), len(debruijn[j-1]) < len(debruijn[j])
                new_i2 = -1
                tmp_num_ids = []
                found = False
                for j2 in range(len(debruijn[j])):
                    _, _, predecessors, add_read_ids = debruijn[j][j2]
                    if len(predecessors) == 0:
                        path_queue.append("%d-%d" % (j, j2))
                    elif i2 in predecessors:
                        found = True
                        # merge into one node
                        if len(predecessors) > 1:
                            merge = True
                        if new_i2 >= 0:
                            branch = True
                        new_i2 = j2
                        tmp_num_ids += add_read_ids

                if merge or branch:
                    for j2 in range(len(debruijn[j])):
                        _, _, predecessors, add_num_ids = debruijn[j][j2]
                        if i2 in predecessors:
                            path_queue.append("%d-%d" % (j, j2))
                    break
                if not found:
                    break
                
                num_ids += tmp_num_ids
                i2 = new_i2
                j += 1

            done.add(i_str)

            num_ids = set(num_ids)
            paths.append([i, j, num_ids])

            if j < len(debruijn) and len(debruijn[j]) == 0:
                j += 1
                while j < len(debruijn) and len(debruijn[j]) == 0:
                    j += 1
                if j < len(debruijn):
                    for j2 in range(len(debruijn[j])):
                        path_queue.append("%d-%d" % (j, j2))
                        

        def get_mate_num_ids(num_ids):
            mate_num_ids = set()
            for num_id in num_ids:
                read_id = num_to_id[num_id]
                mate_read_id = get_mate_node_id(read_id)
                if mate_read_id in id_to_num:
                    mate_num_id = id_to_num[mate_read_id]
                    mate_num_ids.add(mate_num_id)
                    
            return mate_num_ids
        

        # Generate a compressed assembly graph
        def path_cmp(a, b):
            if a[0] != b[0]:
                return a[0] - b[0]
            else:
                return a[1] - b[1]
        paths = sorted(paths, cmp=path_cmp)

        # DK - debugging purposes
        for p in range(len(paths)):
            print >> sys.stderr, "path:", p, paths[p]

        excl_num_ids = set() # exclusive num ids
        equiv_list = []
        p = 0
        while p < len(paths):
            left, right, num_ids = paths[p]
            p2 = p + 1
            while p2 < len(paths):
                next_left, next_right, next_num_ids = paths[p2]
                if next_left >= right:
                    break
                p2 += 1

            equiv_list.append([])
            for i in range(p, p2):
                left, right, num_ids = paths[i]
                equiv_list[-1].append([[i], num_ids, num_ids | get_mate_num_ids(num_ids)])
                if p + 1 < p2:
                    assert p + 2 == p2
                    excl_num_ids |= num_ids

            p = p2

        new_equiv_list = []
        for classes in equiv_list:
            if len(classes) > 1:
                new_equiv_list.append(classes)
                continue
            assert len(classes) == 1
            num_ids = classes[0][1] - excl_num_ids
            if len(num_ids) <= 0:
                continue
            classes[0][1] = num_ids
            classes[0][2] = num_ids | get_mate_num_ids(num_ids)
            new_equiv_list.append(classes)
        equiv_list = new_equiv_list

        while True:
            # DK - debugging purposes
            # """
            for i in range(len(equiv_list)):
                classes = equiv_list[i]
                for j in range(len(classes)):
                    ids, num_ids, all_ids = classes[j]
                    print >> sys.stderr, i, j, ids, len(num_ids), sorted(list(num_ids))[:20]

                print >> sys.stderr
            # """
            
            best_common_mat, best_stat, best_i, best_i2 = [], -sys.maxint, -1, -1
            for i in range(len(equiv_list) - 1):
                classes = equiv_list[i]
                for i2 in range(i + 1, len(equiv_list)):
                    classes2 = equiv_list[i2]
                    common_mat = []
                    for j in range(len(classes)):
                        common_mat.append([])
                        ids = classes[j][2]
                        for j2 in range(len(classes2)):
                            ids2 = classes2[j2][2]
                            common_mat[-1].append(len(ids & ids2))

                    # Calculate stat
                    common_stat = 0
                    if len(classes) == 1 or len(classes2) == 1:
                        for row in common_mat:
                            common_stat += sum(row)
                    else:
                        for row in common_mat:
                            sorted_row = sorted(row, reverse=True)
                            common_stat += (sorted_row[0] - sorted_row[1])
                        if common_mat[0][0] + common_mat[1][1] == \
                           common_mat[1][0] + common_mat[0][1]:
                            common_stat = -1

                    if common_stat > best_stat:
                        best_common_mat, best_stat, best_i, best_i2 = common_mat,  common_stat, i, i2

            # DK - debugging purposes
            # """
            print >> sys.stderr, "best:", best_i, best_i2, best_stat, best_common_mat
            print >> sys.stderr
            print >> sys.stderr
            # """

            if best_stat < 0:
                break

            # DK - for the moment
            mat = best_common_mat
            classes, classes2 = equiv_list[best_i], equiv_list[best_i2]

            # Filter vertices further if necessary
            def del_row(classes, mat, r):
                return classes[:r] + classes[r+1:], mat[:r] + mat[r+1:]
            
            def del_col(classes, mat, c):                    
                new_mat = []
                for row in mat:
                    row = row[:c] + row[c+1:]
                    new_mat.append(row)
                return classes[:c] + classes[c+1:], new_mat
                
            assert len(classes) <= 2 and len(classes2) <= 2
            if len(classes) == 2 and len(classes2) == 2:
                # Check row
                num_ids1, num_ids2 = len(classes[0][1]), len(classes[1][1])
                if num_ids1 * 6 < num_ids2 or num_ids2 * 6 < num_ids1:
                    row_sum1, row_sum2 = sum(mat[0]), sum(mat[1])
                    if row_sum1 > max(2, row_sum2 * 6):
                        classes, mat = del_row(classes, mat, 1)
                        classes[0][1] -= excl_num_ids
                    elif row_sum2 > max(2, row_sum1 * 6):
                        classes, mat = del_row(classes, mat, 0)
                        classes[0][1] -= excl_num_ids
                # Check column
                if len(classes) == 2:
                    num_ids1, num_ids2 = len(classes2[0][1]), len(classes2[1][1])
                    if num_ids1 * 6 < num_ids2 or num_ids2 * 6 < num_ids1:
                        col_sum1, col_sum2 = mat[0][0] + mat[1][0], mat[0][1] + mat[1][1]
                        if col_sum1 > max(2, col_sum2 * 6):
                            classes2, mat = del_col(classes2, mat, 1)
                            classes2[0][1] -= excl_num_ids
                        elif col_sum2 > max(2, col_sum1 * 6):
                            classes2, mat = del_col(classes2, mat, 0)
                            classes2[0][1] -= excl_num_ids
                        
            if len(classes) == 1 and len(classes2) == 1:
                classes[0][0] = sorted(classes[0][0] + classes2[0][0])
                classes[0][1] |= classes2[0][1]
            elif len(classes) == 1:
                if 0 not in classes[0][0] and mat[0][0] > max(2, mat[0][1] * 6):
                    classes[0][0] = sorted(classes[0][0] + classes2[0][0])
                    classes[0][1] |= classes2[0][1]
                elif 0 not in classes[0][0] and mat[0][1] > max(2, mat[0][0] * 6):
                    classes[0][0] = sorted(classes[0][0] + classes2[1][0])
                    classes[0][1] |= classes2[1][1]
                else:
                    classes.append(deepcopy(classes[0]))
                    # Handle a special case at 5' end
                    if 0 in classes[0][0] and len(classes[0][0]) == 1 and mat[0][0] != mat[0][1]:
                        if mat[0][0] > mat[0][1]:
                            classes[0][0] = sorted(classes[0][0] + classes2[0][0])
                            classes[0][1] |= classes2[0][1]
                            classes[1] = classes2[1]
                        else:
                            assert mat[0][1] > mat[0][0]
                            classes[0] = classes2[0]
                            classes[1][0] = sorted(classes[1][0] + classes2[1][0])
                            classes[1][1] |= classes2[1][1]
                    else:
                        classes[0][0] = sorted(classes[0][0] + classes2[0][0])
                        classes[0][1] |= classes2[0][1]
                        classes[1][0] = sorted(classes[1][0] + classes2[1][0])
                        classes[1][1] |= classes2[1][1]
            elif len(classes2) == 1:
                if mat[0][0] > max(2, mat[1][0] * 6):
                    classes[0][0] = sorted(classes[0][0] + classes2[0][0])
                    classes[0][1] |= classes2[0][1]
                    if len(classes[0][1]) > len(classes[1][1]) * 6:
                        classes = [classes[0]]
                elif mat[1][0] > max(2, mat[0][0] * 6):
                    classes[1][0] = sorted(classes[1][0] + classes2[0][0])
                    classes[1][1] |= classes2[0][1]
                    if len(classes[1][1]) > len(classes[0][1]) * 6:
                        classes = [classes[1]]
                else:
                    classes[0][0] = sorted(classes[0][0] + classes2[0][0])
                    classes[0][1] |= classes2[0][1]
                    classes[1][0] = sorted(classes[1][0] + classes2[0][0])
                    classes[1][1] |= classes2[0][1]
            else:                
                score00 = mat[0][0] + mat[1][1]
                score01 = mat[0][1] + mat[1][0]
                if score00 > score01:
                    classes[0][0] = sorted(classes[0][0] + classes2[0][0])
                    classes[0][1] |= classes2[0][1]
                    classes[1][0] = sorted(classes[1][0] + classes2[1][0])
                    classes[1][1] |= classes2[1][1]
                elif score00 < score01:
                    classes[0][0] = sorted(classes[0][0] + classes2[1][0])
                    classes[0][1] |= classes2[1][1]
                    classes[1][0] = sorted(classes[1][0] + classes2[0][0])
                    classes[1][1] |= classes2[0][1]
                else:
                    break

            for c in range(len(classes)):
                classes[c][2] = classes[c][1] | get_mate_num_ids(classes[c][1])
                
            equiv_list[best_i] = classes            
            equiv_list = equiv_list[:best_i2] + equiv_list[best_i2+1:]

        new_nodes = {}
        for i in range(len(equiv_list)):
            classes = equiv_list[i]
            for j in range(len(classes)):
                ids, num_ids, all_ids = classes[j]
                num_ids = sorted(list(num_ids))

                # DK - debugging purposes
                print >> sys.stderr, i, j, num_ids
                
                assert (num_ids) > 0
                read_id = num_to_id[num_ids[0]]
                node = deepcopy(self.nodes[read_id])
                for num_id2 in num_ids[1:]:
                    read_id2 = num_to_id[num_id2]
                    node2 = self.nodes[read_id2]
                    node.combine_with(node2)

                new_read_id = "(%d-%d)%s" % (i, j, read_id)
                node.id = new_read_id
                new_read_id not in new_nodes
                new_nodes[new_read_id] = node
        self.nodes = new_nodes            
            
        # DK - debugging purposes
        # sys.exit(1)


        
class HtmlDraw:
    def __init__(self, base_fname):
        self.base_fname = base_fname

        
    def write_html_css(self, width = 2000, height = 1000):
        base_fname = self.base_fname
        html_file = open("%s.html" % base_fname, 'w')
        print >> html_file, r'<!DOCTYPE html>'
        print >> html_file, r'<html>'
        print >> html_file, r'<head>'
        print >> html_file, r'<title>HISAT-genotyping HLA</title>'
        print >> html_file, r'<link rel="stylesheet" type="text/css" href="%s.css"/>' % base_fname
        print >> html_file, r'</head>'
        print >> html_file, r'<body>'
        print >> html_file, r'<canvas id="a" width="%d" height="%d">' % (width, height)
        print >> html_file, r'This text is displayed if your browser does not support HTML5 Canvas.'
        print >> html_file, r'</canvas>'
        print >> html_file, r'<script type="text/javascript" src="%s.js"></script>' % base_fname
        print >> html_file, r'</body>'
        print >> html_file, r'</html>'
        html_file.close()

        css_file = open("%s.css" % base_fname, 'w')
        print >> css_file, r'canvas {'
        print >> css_file, r'border: 1px dotted black;'
        print >> css_file, r'}'
        css_file.close()

        
    def start_js(self):
        self.js_file = open("%s.js" % self.base_fname, 'w')
        print >> self.js_file, r'var a_canvas = document.getElementById("a");'
        print >> self.js_file, r'var ctx = a_canvas.getContext("2d");'

        
    def end_js(self):
        self.js_file.close()

        
    def draw_smile(self):
        js_file = self.js_file
        
        # Draw the face
        print >> js_file, r'ctx.fillStyle = "yellow";'
        print >> js_file, r'ctx.beginPath();'
        print >> js_file, r'ctx.arc(95, 85, 40, 0, 2*Math.PI);'
        print >> js_file, r'ctx.closePath();'
        print >> js_file, r'ctx.fill();'
        print >> js_file, r'ctx.lineWidth = 2;'
        print >> js_file, r'ctx.stroke();'
        print >> js_file, r'ctx.fillStyle = "black";'
        
        # Draw the left eye
        print >> js_file, r'ctx.beginPath();'
        print >> js_file, r'ctx.arc(75, 75, 5, 0, 2*Math.PI);'
        print >> js_file, r'ctx.closePath();'
        print >> js_file, r'ctx.fill();'

        # Draw the right eye
        print >> js_file, r'ctx.beginPath();'
        print >> js_file, r'ctx.arc(114, 75, 5, 0, 2*Math.PI);'
        print >> js_file, r'ctx.closePath();'
        print >> js_file, r'ctx.fill();'

        # Draw the mouth
        print >> js_file, r'ctx.beginPath();'
        print >> js_file, r'ctx.arc(95, 90, 26, Math.PI, 2*Math.PI, true);'
        print >> js_file, r'ctx.closePath();'
        print >> js_file, r'ctx.fill();'

        # Write "Hello, World!"
        print >> js_file, r'ctx.font = "30px Garamond";'
        print >> js_file, r'ctx.fillText("Hello, World!", 15, 175);'
       
