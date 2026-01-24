#include <iostream>
#include <fstream>
#include <vector>
#include <queue>
#include <map>
#include <string>
#include <algorithm>

using namespace std;

class BitWriter {
public:
    string buffer;
    ofstream& out;
    
    BitWriter(ofstream& os) : out(os), buffer("") {}
    
    void writeBit(int bit) {
        buffer += (bit ? '1' : '0');
        if (buffer.length() == 8) {
            flush();
        }
    }
    
    void writeByte(unsigned char byte) {
        for (int i = 7; i >= 0; i--) {
            writeBit((byte >> i) & 1);
        }
    }
    
    void flush() {
        if (buffer.empty()) return;
        while (buffer.length() < 8) buffer += '0'; // Pad with zeros
        unsigned char byte = (unsigned char)stoi(buffer, nullptr, 2);
        out.write(reinterpret_cast<char*>(&byte), sizeof(byte));
        buffer = "";
    }
};

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

class OptimizedHuffman {
private:
    Node* root;
    map<unsigned char, string> codes;
    map<unsigned char, int> freqMap;

    void deleteTree(Node* node) {
        if (!node) return;
        deleteTree(node->left);
        deleteTree(node->right);
        delete node;
    }

    void generateCodes(Node* node, string str) {
        if (!node) return;
        if (!node->left && !node->right) {
            codes[node->ch] = str;
        }
        generateCodes(node->left, str + "0");
        generateCodes(node->right, str + "1");
    }

    void writeTree(Node* node, BitWriter& writer) {
        if (!node->left && !node->right) {
            writer.writeBit(1);
            writer.writeByte(node->ch);
        } else {
            writer.writeBit(0);
            writeTree(node->left, writer);
            writeTree(node->right, writer);
        }
    }

    Node* readTree(BitReader& reader) {
        int bit = reader.readBit();
        if (bit == 1) {
            unsigned char c = reader.readByte();
            return new Node(c, 0);
        } else {
            Node* left = readTree(reader);
            Node* right = readTree(reader);
            return new Node('\0', 0, left, right);
        }
    }

public:
    OptimizedHuffman() : root(nullptr) {}
    ~OptimizedHuffman() { deleteTree(root); }

    void compress(string inputFile, string outputFile) {
        ifstream in(inputFile, ios::binary);
        if (!in.is_open()) {
            cerr << "Error: Could not open input file." << endl;
            return;
        }
        
        // Read entire file
        string text((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
        in.close();

        if (text.empty()) return;

        // 1. Frequency Map
        for (unsigned char c : text) freqMap[c]++;
        
        // 2. Build Tree
        priority_queue<Node*, vector<Node*>, Compare> pq;
        for (auto p : freqMap) pq.push(new Node(p.first, p.second));
        
        if (pq.size() == 1) {
             Node* onlyNode = pq.top(); pq.pop();
             root = new Node('\0', onlyNode->freq, onlyNode, nullptr);
             codes[onlyNode->ch] = "0"; 
        } else {
            while (pq.size() > 1) {
                Node* l = pq.top(); pq.pop();
                Node* r = pq.top(); pq.pop();
                pq.push(new Node('\0', l->freq + r->freq, l, r));
            }
            root = pq.top();
            generateCodes(root, "");
        }

        ofstream out(outputFile, ios::binary);
        BitWriter writer(out);

        int totalChars = text.size();
        out.write(reinterpret_cast<char*>(&totalChars), sizeof(totalChars));

        writeTree(root, writer);

        for (unsigned char c : text) {
            string code = codes[c];
            for (char bit : code) {
                writer.writeBit(bit - '0');
            }
        }
        writer.flush();

        long compressedSize = out.tellp();
        out.close();

        // 6. Output JSON Stats for Node.js
        cout << "JSON_RESULT:{";
        cout << "\"originalSize\":" << totalChars << ",";
        cout << "\"compressedSize\":" << compressedSize << ",";
        cout << "\"codes\":{";
        bool first = true;
        for(auto const& pair : codes) {
            unsigned char key = pair.first;
            string val = pair.second;

            if(!first) cout << ",";
            cout << "\"" << (int)key << "\":\"" << val << "\"";
            first = false;
        }
        cout << "}}";
        cout << endl;
    }

    void decompress(string inputFile, string outputFile) {
        ifstream in(inputFile, ios::binary);
        if (!in.is_open()) {
            cerr << "Error: Could not open input file." << endl;
            return;
        }

        // 1. Read Total Characters
        int totalChars;
        if(!in.read(reinterpret_cast<char*>(&totalChars), sizeof(totalChars))) return;

        BitReader reader(in);

        // 2. Rebuild Tree
        root = readTree(reader);

        // 3. Decode
        ofstream out(outputFile, ios::binary);
        Node* curr = root;
        int charsDecoded = 0;

        while (charsDecoded < totalChars) {
            int bit = reader.readBit();
            if (bit == -1) break; 

            if (bit == 0) {
                 if (curr->left) curr = curr->left;
            } else {
                 if (curr->right) curr = curr->right;
            }

            if (!curr->left && !curr->right) {
                out.put(curr->ch);
                charsDecoded++;
                curr = root;
            }
        }
        out.close();
        
        cout << "JSON_RESULT:{\"message\":\"Decompression successful\"}" << endl;
    }
};

int main(int argc, char* argv[]) {
    if(argc < 4) return 1;
    string action = argv[1];
    OptimizedHuffman huff;
    if(action == "compress") huff.compress(argv[2], argv[3]);
    else huff.decompress(argv[2], argv[3]);
    return 0;
}