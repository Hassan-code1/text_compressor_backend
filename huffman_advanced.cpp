#include <iostream>
#include <fstream>
#include <vector>
#include <queue>
#include <map>
#include <string>
#include <algorithm>
#include <cstdint>

using namespace std;

// ── 64-bit BitWriter ──────────────────────────────────────────────────────────
// Accumulates bits in a uint64_t buffer (MSB = first bit written).
// Flushes a full byte to disk each time 8 bits accumulate.
// Much faster than the old string-based approach.
class BitWriter {
    uint64_t buf   = 0;  // bits packed from bit-63 downward
    int      count = 0;  // number of valid bits in buf
    ofstream& out;

    void flushByte(int shift) {
        unsigned char byte = (unsigned char)((buf >> shift) & 0xFF);
        out.write(reinterpret_cast<char*>(&byte), 1);
    }

public:
    explicit BitWriter(ofstream& os) : out(os) {}

    void writeBit(int bit) {
        if (bit) buf |= (1ULL << (63 - count));
        if (++count == 64) {
            for (int i = 56; i >= 0; i -= 8) flushByte(i);
            buf = 0; count = 0;
        }
    }

    void writeByte(unsigned char byte) {
        for (int i = 7; i >= 0; i--) writeBit((byte >> i) & 1);
    }

    void flush() {
        if (!count) return;
        int bytes = (count + 7) / 8;
        for (int i = 0; i < bytes; i++) flushByte(56 - i * 8);
        buf = 0; count = 0;
    }
};

// ── BitReader (unchanged) ─────────────────────────────────────────────────────
class BitReader {
public:
    ifstream& in;
    unsigned char currentByte;
    int bitPos;
    BitReader(ifstream& is) : in(is), bitPos(8) {}

    int readBit() {
        if (bitPos == 8) {
            if (!in.read(reinterpret_cast<char*>(&currentByte), 1)) return -1;
            bitPos = 0;
        }
        int bit = (currentByte >> (7 - bitPos)) & 1;
        bitPos++;
        return bit;
    }

    unsigned char readByte() {
        unsigned char byte = 0;
        for (int i = 0; i < 8; i++) {
            int bit = readBit();
            byte = (byte << 1) | bit;
        }
        return byte;
    }
};

// ── Huffman Tree Node ─────────────────────────────────────────────────────────
struct Node {
    unsigned char ch;
    int freq;
    Node *left, *right;
    Node(unsigned char c, int f, Node* l = nullptr, Node* r = nullptr)
        : ch(c), freq(f), left(l), right(r) {}
};

struct Compare {
    bool operator()(Node* l, Node* r) { return l->freq > r->freq; }
};

// ── RLE Pre-Processor ─────────────────────────────────────────────────────────
// Finds an unused byte as a sentinel so encoding is always unambiguous.
// Run encoding: [sentinel][count][byte] for run of count >= 4.
// Literal sentinel: [sentinel][1][sentinel].
// Returns sentinel=-1 when all 256 byte values appear (RLE skipped).
struct RleResult { int sentinel; string data; };

RleResult rleEncode(const string& text) {
    bool used[256] = {};
    for (unsigned char c : text) used[(int)c] = true;
    int sentinel = -1;
    for (int i = 0; i < 256; i++) { if (!used[i]) { sentinel = i; break; } }
    if (sentinel < 0) return {-1, ""};

    unsigned char S = (unsigned char)sentinel;
    string out; out.reserve(text.size());
    int i = 0, n = (int)text.size();
    while (i < n) {
        unsigned char c = (unsigned char)text[i];
        int run = 1;
        while (i + run < n && (unsigned char)text[i + run] == c && run < 255) run++;
        if (c == S) {
            out += (char)S; out += (char)1; out += (char)S; i++;
        } else if (run >= 4) {
            out += (char)S; out += (char)run; out += (char)c; i += run;
        } else {
            for (int k = 0; k < run; k++) out += (char)c; i += run;
        }
    }
    return {sentinel, out};
}

string rleDecode(const string& text, int sentinel) {
    unsigned char S = (unsigned char)sentinel;
    string out; out.reserve(text.size() * 2);
    int i = 0, n = (int)text.size();
    while (i < n) {
        unsigned char c = (unsigned char)text[i];
        if (c == S && i + 2 < n) {
            int  count = (unsigned char)text[i + 1];
            char sym   = text[i + 2];
            for (int k = 0; k < count; k++) out += sym;
            i += 3;
        } else { out += (char)c; i++; }
    }
    return out;
}

// ── Build a decode tree from a canonical code map ─────────────────────────────
static Node* insertCanonicalCode(Node* root, const string& code, unsigned char sym) {
    if (!root) root = new Node('\0', 0);
    Node* curr = root;
    for (char bit : code) {
        if (bit == '0') {
            if (!curr->left)  curr->left  = new Node('\0', 0);
            curr = curr->left;
        } else {
            if (!curr->right) curr->right = new Node('\0', 0);
            curr = curr->right;
        }
    }
    curr->ch = sym;
    return root;
}

// ── Main Compressor/Decompressor ──────────────────────────────────────────────
class OptimizedHuffman {
private:
    Node* root = nullptr;
    map<unsigned char, string> codes;   // canonical codes (symbol → bits)
    map<unsigned char, int>    freqMap;
    uint8_t codeLengths[256] = {};      // code length for each byte (0 = unused)

    // --- Step tracking for frontend animation ---
    struct StepInfo { int lCh, lFreq, rCh, rFreq, combined; };
    vector<StepInfo> buildSteps;

    void deleteTree(Node* node) {
        if (!node) return;
        deleteTree(node->left);
        deleteTree(node->right);
        delete node;
    }

    // Traverse tree to record code lengths (not the codes themselves)
    void extractLengths(Node* node, int depth) {
        if (!node) return;
        if (!node->left && !node->right) {
            codeLengths[node->ch] = (uint8_t)depth;
            return;
        }
        extractLengths(node->left,  depth + 1);
        extractLengths(node->right, depth + 1);
    }

    // From codeLengths[], build canonical codes and populate codes[]
    void buildCanonicalCodes() {
        codes.clear();
        // Collect (length, symbol) pairs for used symbols
        vector<pair<int,int>> syms;
        for (int i = 0; i < 256; i++)
            if (codeLengths[i] > 0) syms.push_back({codeLengths[i], i});
        if (syms.empty()) return;
        sort(syms.begin(), syms.end());

        uint32_t code = 0;
        int prevLen   = 0;
        for (auto& p : syms) {
            int len = p.first, sym = p.second;
            if (prevLen > 0) {
                code++;
                code <<= (len - prevLen);
            }
            // Convert numeric code to binary string of exactly 'len' digits
            string codeStr(len, '0');
            for (int i = len - 1; i >= 0; i--) {
                codeStr[i] = ((code >> (len - 1 - i)) & 1) ? '1' : '0';
            }
            codes[(unsigned char)sym] = codeStr;
            prevLen = len;
        }
    }

    // Rebuild decode tree from canonical codes map (for decompression)
    Node* buildDecodeTree() {
        Node* r = nullptr;
        for (auto& p : codes) r = insertCanonicalCode(r, p.second, p.first);
        return r;
    }

    // Serialize tree node to JSON for the frontend visualizer
    string serializeNode(Node* node) {
        if (!node) return "null";
        string s = "{";
        if (!node->left && !node->right) {
            s += "\"isLeaf\":true,\"ch\":" + to_string((int)node->ch);
            s += ",\"freq\":" + to_string(node->freq);
            s += ",\"code\":\"" + codes[node->ch] + "\"";
        } else {
            s += "\"isLeaf\":false,\"freq\":" + to_string(node->freq);
            s += ",\"left\":"  + serializeNode(node->left);
            s += ",\"right\":" + serializeNode(node->right);
        }
        return s + "}";
    }

public:
    OptimizedHuffman() {}
    ~OptimizedHuffman() { deleteTree(root); }

    // ── COMPRESS ─────────────────────────────────────────────────────────────
    bool compress(string inputFile, string outputFile, bool useRle = false) {
        ifstream in(inputFile, ios::binary);
        if (!in.is_open()) { cerr << "Error: Cannot open input file.\n"; return false; }

        string text((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
        in.close();
        if (text.empty()) return false;

        // 0. Optional RLE pre-processing
        int rleSentinel = -1;
        if (useRle) {
            RleResult r = rleEncode(text);
            if (r.sentinel >= 0) { text = r.data; rleSentinel = r.sentinel; }
        }

        // 1. Frequency map
        for (unsigned char c : text) freqMap[c]++;

        // 2. Build Huffman tree (for code lengths + visualization)
        priority_queue<Node*, vector<Node*>, Compare> pq;
        for (auto& p : freqMap) pq.push(new Node(p.first, p.second));

        if (pq.size() == 1) {
            Node* only = pq.top(); pq.pop();
            root = new Node('\0', only->freq, only, nullptr);
            codeLengths[only->ch] = 1;
        } else {
            while (pq.size() > 1) {
                Node* l = pq.top(); pq.pop();
                Node* r = pq.top(); pq.pop();
                if ((int)buildSteps.size() < 60) {
                    buildSteps.push_back({
                        (!l->left && !l->right) ? (int)l->ch : -1, l->freq,
                        (!r->left && !r->right) ? (int)r->ch : -1, r->freq,
                        l->freq + r->freq
                    });
                }
                pq.push(new Node('\0', l->freq + r->freq, l, r));
            }
            root = pq.top();
            // Extract code lengths via tree traversal
            extractLengths(root, 0);
        }

        // 3. Build canonical codes from lengths
        buildCanonicalCodes();

        // 4. Write file: [1B rle_flag][1B sentinel][4B totalChars][256B codeLengths][bits]
        ofstream out(outputFile, ios::binary);
        if (!out.is_open()) { cerr << "Error: Cannot open output file.\n"; return false; }

        uint8_t rleFlag = (rleSentinel >= 0) ? 1 : 0;
        uint8_t rleSen  = (rleSentinel >= 0) ? (uint8_t)rleSentinel : 0;
        out.write(reinterpret_cast<char*>(&rleFlag), 1);
        out.write(reinterpret_cast<char*>(&rleSen),  1);

        int totalChars = (int)text.size();
        // Header
        out.write(reinterpret_cast<char*>(&totalChars), sizeof(totalChars));
        out.write(reinterpret_cast<char*>(codeLengths), 256);

        // Bit-stream
        BitWriter writer(out);
        for (unsigned char c : text) {
            const string& code = codes[c];
            for (char bit : code) writer.writeBit(bit - '0');
        }
        writer.flush();

        long compressedSize = out.tellp();
        out.close();

        // 5. JSON stats for Node.js
        cout << "JSON_RESULT:{";
        cout << "\"originalSize\":"   << totalChars      << ",";
        cout << "\"compressedSize\":" << compressedSize  << ",";
        cout << "\"rle\":"            << (rleFlag ? "true" : "false") << ",";
        cout << "\"codes\":{";
        bool first = true;
        for (auto const& p : codes) {
            unsigned char key = p.first;
            string val = p.second;
            if (!first) cout << ",";
            cout << "\"" << (int)key << "\":\"" << val << "\"";
            first = false;
        }
        cout << "}";
        // Frequency map
        cout << ",\"freqs\":{";
        bool fFirst = true;
        for (auto const& p : freqMap) {
            if (!fFirst) cout << ",";
            cout << "\"" << (int)p.first << "\":" << p.second;
            fFirst = false;
        }
        cout << "}";
        // Serialized tree (for visualizer)
        cout << ",\"tree\":" << serializeNode(root);
        // Merge steps (for animation)
        cout << ",\"steps\":[";
        for (int i = 0; i < (int)buildSteps.size(); i++) {
            if (i > 0) cout << ",";
            auto& s = buildSteps[i];
            cout << "{\"l\":{\"ch\":" << s.lCh << ",\"freq\":" << s.lFreq << "}";
            cout << ",\"r\":{\"ch\":" << s.rCh << ",\"freq\":" << s.rFreq << "}";
            cout << ",\"combined\":" << s.combined << "}";
        }
        cout << "]";
        // Bit-stream preview (first 80 characters)
        cout << ",\"bitPreview\":[";
        int prevN = min((int)text.size(), 80);
        for (int i = 0; i < prevN; i++) {
            if (i > 0) cout << ",";
            unsigned char c = (unsigned char)text[i];
            cout << "{\"ch\":" << (int)c << ",\"bits\":\"" << codes[c] << "\"}";
        }
        cout << "]}" << endl;

        return true;
    }

    // ── DECOMPRESS ───────────────────────────────────────────────────────────
    bool decompress(string inputFile, string outputFile) {
        ifstream in(inputFile, ios::binary);
        if (!in.is_open()) { cerr << "Error: Cannot open input file.\n"; return false; }

        // Read 2-byte RLE header, then canonical Huffman header
        uint8_t rleFlag = 0, rleSen = 0;
        if (!in.read(reinterpret_cast<char*>(&rleFlag), 1)) return false;
        if (!in.read(reinterpret_cast<char*>(&rleSen),  1)) return false;
        int totalChars = 0;
        if (!in.read(reinterpret_cast<char*>(&totalChars), sizeof(totalChars))) return false;
        if (!in.read(reinterpret_cast<char*>(codeLengths), 256)) return false;

        // Reconstruct canonical codes then build decode tree
        buildCanonicalCodes();
        root = buildDecodeTree();
        if (!root) return false;

        // Decode Huffman bit-stream into a string buffer
        string decoded;
        decoded.reserve(totalChars);
        int charsDecoded = 0;
        BitReader reader(in);

        if (!root->left && !root->right) {
            while (charsDecoded < totalChars) { reader.readBit(); decoded += (char)root->ch; charsDecoded++; }
        } else {
            Node* curr = root;
            while (charsDecoded < totalChars) {
                int bit = reader.readBit();
                if (bit == -1) break;
                curr = (bit == 0) ? curr->left : curr->right;
                if (!curr) break;
                if (!curr->left && !curr->right) { decoded += (char)curr->ch; charsDecoded++; curr = root; }
            }
        }

        // Optional RLE post-decode
        if (rleFlag) decoded = rleDecode(decoded, (int)rleSen);

        ofstream out(outputFile, ios::binary);
        out.write(decoded.data(), decoded.size());
        out.close();

        cout << "JSON_RESULT:{\"message\":\"Decompression successful\"}" << endl;
        return true;
    }
};

// ── Entry point ───────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    if (argc < 4) return 1;
    string action = argv[1];
    bool useRle = false;
    for (int i = 4; i < argc; i++) if (string(argv[i]) == "--rle") useRle = true;
    OptimizedHuffman huff;
    bool ok = (action == "compress")
        ? huff.compress(argv[2], argv[3], useRle)
        : huff.decompress(argv[2], argv[3]);
    return ok ? 0 : 1;
}