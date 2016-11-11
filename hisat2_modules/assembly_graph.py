#!/usr/bin/env python

import sys
import math
import random
from copy import deepcopy


#
def get_major_nt(nt_dic):
    nt = ''
    max_count = 0
    for tmp_nt, tmp_count in nt_dic.items():
        assert nt in "ACGTD"
        if tmp_count > max_count:
            max_count = tmp_count
            nt = tmp_nt
    assert nt in "ACGTD"
    return nt                


#
def match_score(nt_dic1, nt_dic2):
    total1, total2 = sum(nt_dic1.values()) * 2.0, sum(nt_dic2.values()) * 2.0
    best = 0.0
    for nt in "ACGT":
        if nt not in nt_dic1 or nt not in nt_dic2:
            continue
        tmp_best = nt_dic1[nt] / total1 + nt_dic2[nt] / total2
        if tmp_best > best:
            best = tmp_best
    return best


#
def get_ungapped_seq(seq):
    ungapped_seq = []
    for nt_dic in seq:
        if get_major_nt(nt_dic) == 'D':
            continue
        ungapped_seq.append(nt_dic)
    return ungapped_seq


class Node:
    # Initialize
    def __init__(self, id, left, seq, var):
        self.next = [] # list of next nodes

        id = id.split('_')[0]
        self.id = id # Node ID

        self.left = left # starting position

        # sequence that node represents
        assert 'I' not in seq
        self.seq = []
        for nt in seq:
            assert nt in "ACGTD"
            self.seq.append({nt : 1})

        # how sequence is related to backbone
        self.var = []
        for v in var:
            self.var.append(set([v]) if v != '' else set())

        assert len(self.seq) == len(self.var)
        self.right = self.left + len(seq) - 1

        # DK -
        self.read_ids = set([id])
        self.mate_ids = set([id.split('|')[0]])

        
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
    def overlap_with(self, other):
        assert self.left <= other.left
        if self.right < other.left:
            return -1, -1

        seq, other_seq = get_ungapped_seq(self.seq), get_ungapped_seq(other.seq)
        for i in range(len(seq)):
            max_mm = 0.01 * (len(seq) - i)
            tmp_mm = 0.0
            for j in range(len(other_seq)):
                if i + j >= len(seq):
                    break
                other_nt_dic = other.seq[j]
                mismatch = 1.0 - match_score(seq[i+j], other_seq[j])
                assert mismatch >= 0.0
                tmp_mm += mismatch
                if tmp_mm > max_mm:
                    break

            if tmp_mm <= max_mm:
                return i, len(seq) - i
                
        return -1, -1

    
    # Combine two nodes with considering deletions
    def combine_with(self, other):
        assert self.left <= other.left
        assert self.right >= other.left
        if self.right >= other.right:
            return

        # Merge two sequences
        assert len(other.seq) > 0 and 'D' not in other.seq[0].keys()
        i, j = other.left - self.left, 0
        new_seq, new_var = self.seq[:i], self.var[:i]
        while i < len(self.seq) and j < len(other.seq):
            nt_dic, nt_var = self.seq[i], self.var[i]
            nt_dic2, nt_var2 = other.seq[j], other.var[j]
            new_seq.append(nt_dic)
            new_var.append(nt_var | nt_var2)
            for nt, count in nt_dic2.items():
                if nt in nt_dic:
                    nt_dic[nt] += count
                else:
                    nt_dic[nt] = count
            i += 1
            j += 1
            
        # Append the rest of the other sequence
        assert i == len(self.seq)
        new_seq += other.seq[j:]
        new_var += other.var[j:]

        self.read_ids |= other.read_ids
        self.mate_ids |= other.mate_ids

        self.seq, self.var = new_seq, new_var
        assert len(self.seq) == len(self.var)
        self.right = self.left + len(self.seq) - 1


    # Return the length of the ungapped sequence
    def ungapped_length(self):
        return len(get_ungapped_seq(self.seq))

    
    # Get variants
    def get_vars(self, Vars):
        vars = []
        for var_i in range(len(self.var)):
            for var in self.var[var_i]:
                assert var in Vars
                if len(vars) > 0 and var == vars[-1]:
                    continue
                type, pos, data = Vars[var]
                nt = get_major_nt(self.seq[var_i])
                if data == nt or \
                   type == "deletion" and nt == 'D':
                    vars.append(var)                    
        return vars

        
    # Display node information
    def print_info(self):
        seq, avg = "", 0
        for nt_dic in self.seq:
            seq += get_major_nt(nt_dic)
            avg += sum(nt_dic.values())
        print >> sys.stderr, "Node ID:", self.id
        print >> sys.stderr, "Pos: [%d, %d], Avg. coverage: %.1f" % (self.left, self.right, float(avg) / len(self.seq))
        print >> sys.stderr, "\t", seq
        prev_var = ""
        for var_i in range(len(self.var)):
            var = self.var[var_i]
            var = '-'.join(list(var))
            if var != "" and var != prev_var:
                print >> sys.stderr, "\t%d: %s" % (var_i, var), self.seq[var_i],
            prev_var = var
        print >> sys.stderr
        print >> sys.stderr, "mates:", sorted(self.mate_ids, key=int)
        # print >> sys.stderr, "reads:", sorted(self.read_ids)

                
class Graph:
    def __init__(self, backbone):
        # self.head = Node()
        self.backbone = backbone # backbone sequence

        self.nodes = {}
        self.edges = {}

        self.left_margin = 20
        self.right_margin = 20
        self.top_margin = 20
        self.bottom_margin = 20

        self.scalex, self.scaley = 5, 2
        self.width = len(self.backbone) * self.scalex + self.left_margin + self.right_margin
        self.height = 2000 * self.scaley


    # Add node, which is an alignment w.r.t. the reference
    def add_node(self, id, node):
        # DK - debugging purposes
        id = id.split('_')[0]
        if id in self.nodes:
            # print >> sys.stderr, "Warning) multi-mapped read:", id
            return
        assert id not in self.nodes
        self.nodes[id] = node


    # Generate edges based on the overlapping information between nodes
    def generate_raw_edges(self, overlap_pct = 0.8):
        assert len(self.nodes) > 0
        nodes = [[id, node.left, node.right] for id, node in self.nodes.items()]
        def node_cmp(a, b):
            return a[1] - b[1]
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
                at, overlap = node1.overlap_with(node2)
                if overlap < node1.ungapped_length() * overlap_pct:
                    continue
                if id1 not in self.to_node:
                    self.to_node[id1] = [[id2, at]]
                else:
                    self.to_node[id1].append([id2, at])
                if id2 not in self.from_node:
                    self.from_node[id2] = [[id1, -at]]
                else:
                    self.from_node[id2].append([id1, -at])

                    
    # Merge and remove nodes inside other nodes, and update edges accordingly
    def merge_inside_nodes(self):
        # Check which nodes are contained within other nodes
        contained_by = {}
        for id1, to_node_ids in self.to_node.items():
            for id2, at in to_node_ids:
                if at == 0:
                    if self.nodes[id1].ungapped_length() >= self.nodes[id2].ungapped_length():
                        contained_by[id2] = id1
                    else:
                        contained_by[id1] = id2
        contain = {}
        for id, up_id in contained_by.items():
            while up_id in contained_by:
                up_id = contained_by[up_id]
            contained_by[id] = up_id
            if up_id not in contain:
                contain[up_id] = set([id])
            else:
                contain[up_id].add(id)

        # Merges nodes with those including them inside
        nodes = {}
        for id, node in self.nodes.items():
            if id in contained_by:
                continue
            nodes[id] = deepcopy(node)
            
        for id, inside_ids in contain.items():
            node = self.nodes[id]
            for id2 in inside_ids:
                node2 = self.nodes[id2]
                node.combine_with(node2)

        # Remove the edges of nodes contained within other nodes
        tmp_to_node, tmp_from_node = {}, {}
        for id1, to_node_ids in self.to_node.items():
            if id1 in contained_by:
                continue
            for id2, at in to_node_ids:
                if id2 in contained_by:
                    continue

                assert id1 in nodes and id2 in nodes
                if id1 not in tmp_to_node:
                    tmp_to_node[id1] = [[id2, at]]
                else:
                    tmp_to_node[id1].append([id2, at])
                if id2 not in tmp_from_node:
                    tmp_from_node[id2] = [[id1, -at]]
                else:
                    tmp_from_node[id2].append([id1, -at])

        self.nodes = nodes
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
    def generate_edges(self, overlap_pct = 0.8):
        self.generate_raw_edges(overlap_pct)
        self.merge_inside_nodes()
        self.remove_redundant_edges()
        
        
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
        self.generate_edges(overlap_pct)

        # DK - debugging purposes
        """
        nodes = [[id, node.left, node.right] for id, node in self.nodes.items()]
        def node_cmp(a, b):
            return a[1] - b[1]
        nodes = sorted(nodes, cmp=node_cmp)
        for id, _, _ in nodes:
            print >> sys.stderr, id, "==>", self.to_node[id] if id in self.to_node else []
            self.nodes[id].print_info(); print >> sys.stderr
        sys.exit(1)
        """

    def informed_assemble(self, params = {"mate": True}):
        mate = "mate" in params and params["mate"]

        # DK - debugging purposes
        if not mate:
            return
        
        # Duplicate nodes when necessary
        iter = 0
        while True and iter < 10:
            iter += 1
            to_node = self.to_node
            from_node = self.from_node
            nodes, new_nodes = self.nodes, {}
            sorted_nodes = [[id, node.left, node.right] for id, node in nodes.items()]
            def node_cmp(a, b):
                return a[1] - b[1]
            sorted_nodes = sorted(sorted_nodes, cmp=node_cmp)
            
            matches_list = []
            for id, _, _ in sorted_nodes:
                if id not in to_node:
                    continue                
                to_ids = [i[0] for i in to_node[id]]
                # id has two successors
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

                if len(from_ids) <= 1 and len(to_ids) <= 1:
                    continue

                if len(from_ids) == 1:
                    for to_id in to_ids:
                        matches.append([id, to_id, 0])
                else:
                    added = set()
                    for to_id in to_ids:
                        max_from_id, max_mate = "", 0
                        for from_id in from_ids:
                            if from_id in added:
                                continue

                            to_ids2 = [i[0] for i in to_node[from_id]] if from_id in to_node else []
                            if to_id not in to_ids2:
                                continue

                            tmp_mate = len(nodes[from_id].mate_ids & nodes[to_id].mate_ids)
                            if max_mate < tmp_mate:
                                max_mate = tmp_mate
                                max_from_id = from_id
                        if max_mate > 0:
                            added.add(max_from_id)
                            matches.append([max_from_id, to_id, max_mate])

                    if len(matches) != 2:
                        continue

                # DK - debugging purposes
                # """
                if len(matches) <= 0:
                    continue
                print >> sys.stderr, "to:", id, "has", to_ids
                print >> sys.stderr, "from:", id, "has", from_ids
                print >> sys.stderr, matches
                for from_id, id, _ in matches:
                    print >> sys.stderr, from_id; nodes[from_id].print_info()
                    print >> sys.stderr, id; nodes[id].print_info()
                print >> sys.stderr
                # sys.exit(1)
                # """

                matches_list.append(matches)

            if len(matches_list) <= 0:
                break

            delete_nodes = set()
            for matches in matches_list:
                for from_id, id, _ in matches:
                    new_id = from_id + '|' + id
                    if new_id in new_nodes:
                        continue
                    from_node, node = deepcopy(nodes[from_id]), nodes[id]; delete_nodes.add(from_id)
                    from_node.id = new_id
                    from_node.combine_with(node); delete_nodes.add(id)                    
                    new_nodes[new_id] = from_node

                    """
                    self.to_node[from_id] = self.to_node[to_id]
                    for to_id2, _ in self.to_node[to_id]:
                        from_nodes = self.from_node[to_id2]
                        replaced = False
                        for i_ in range(len(from_nodes)):
                            if from_nodes[i_][0] == to_id:
                                replaced = True
                                from_nodes[i_][0] = 
                        assert replaced
                    """
            
            for id, node in nodes.items():
                if id in delete_nodes or id in new_nodes:
                    continue
                new_nodes[id] = node

            self.nodes = new_nodes
            self.generate_edges(0.02)
            self.reduce(0.02)


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
        self.informed_assemble({"allele" : True, "alleles" : 0})

        
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
        print >> js_file, r'ctx.font = "12px Serif";'

        # Draw vertical dotted lines at every 100nt and thick lines at every 500nt
        print >> js_file, r'ctx.fillStyle = "gray";'
        for pos in range(100, nodes[-1][2], 100):
            if pos % 500 == 0:
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

            # Draw label
            print >> js_file, r'ctx.fillStyle = "blue";'
            print >> js_file, r'ctx.fillText("%d", %d, %d);' % \
                (pos, get_x(pos+2), get_y(200))

        print >> js_file, r'ctx.setLineDash([]);'


    # End drawing graph
    def end_draw(self):
        self.htmlDraw.end_js()

        
    # Draw graph
    #   Top left as (0, 0) and Bottom right as (width, height)
    def draw(self,
             begin_y,
             title = "",
             second_allele = sys.maxint):
        assert len(self.nodes) > 0
        nodes = [[id, node.left, node.right] for id, node in self.nodes.items()]
        def node_cmp(a, b):
            return a[1] - b[1]
        nodes = sorted(nodes, cmp=node_cmp)

        # display space
        dspace = [[[begin_y, 1000]]] * (nodes[-1][2] + 100)
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

        # Draw nodes
        node_to_y = {}
        draw_title = False
        for id, left, right in nodes:
            read_id, mate = id.split('|')[:2]
            mate = mate.split('_')[0]

            # Get y position
            y = get_dspace(left, right, 14)
            node_to_y[id] = y

            if int(read_id) < second_allele:
                color = "yellow"
            else:
                color = "green"

            # Draw node
            print >> js_file, r'ctx.beginPath();'
            print >> js_file, r'ctx.rect(%d, %d, %d, %d);' % \
                (get_x(left), get_y(y), get_x(right) - get_x(left), get_sy(10))
            print >> js_file, r'ctx.fillStyle = "%s";' % color
            print >> js_file, r'ctx.fill();'
            print >> js_file, r'ctx.lineWidth = 2;'
            print >> js_file, r'ctx.strokeStyle = "black";'
            print >> js_file, r'ctx.stroke();'

            # Draw label
            print >> js_file, r'ctx.fillStyle = "blue";'
            print >> js_file, r'ctx.fillText("%s %s", %d, %d);' % \
                (read_id, mate, get_x(left + 2), get_y(y + 7))

            if not draw_title:
                draw_title = True
                print >> js_file, r'ctx.font = "24px Serif";'
                print >> js_file, r'ctx.fillText("%s", %d, %d);' % \
                    (title, get_x(10), get_y(y + 7))
                print >> js_file, r'ctx.font = "12px Serif";'


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
       
