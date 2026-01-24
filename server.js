const express = require('express');
const multer = require('multer');
const { execFile } = require('child_process');
const path = require('path');
const fs = require('fs');
const cors = require('cors');
const os = require('os');

const app = express();

const PORT = process.env.PORT || 3001;
const FRONTEND_URL = process.env.FRONTEND_URL || "http://localhost:5173";

app.use(cors({
  origin: FRONTEND_URL, 
  methods: ["POST", "GET"],
  credentials: true
}));

const storage = multer.diskStorage({
  destination: os.tmpdir(), 
  filename: (req, file, cb) => cb(null, "huff_" + Date.now() + "_" + file.originalname)
});
const upload = multer({ storage: storage });

const cleanupFile = (filePath) => {
  if (fs.existsSync(filePath)) {
    fs.unlink(filePath, (err) => {
      if (err) console.error(`Error deleting ${filePath}:`, err);
      else console.log(`Cleaned up: ${filePath}`);
    });
  }
};

const runHuffmanTool = (action, inputPath, outputPath, res) => {
  const binaryPath = './huffman_tool';

  execFile(binaryPath, [action, inputPath, outputPath], (error, stdout, stderr) => {
    
    cleanupFile(inputPath);

    if (error) {
      console.error('Exec error:', error);
      cleanupFile(outputPath);
      return res.status(500).json({ error: `${action} failed`, details: stderr });
    }

    const lines = stdout.split('\n');
    const jsonLine = lines.find(line => line.startsWith('JSON_RESULT:'));
    
    let stats = {};
    if (jsonLine) {
      try {
        stats = JSON.parse(jsonLine.replace('JSON_RESULT:', ''));
      } catch (e) {
        console.error("JSON Parse error:", e);
      }
    }

    setTimeout(() => {
      cleanupFile(outputPath);
    }, 15 * 60 * 1000); // 15 Minutes

    res.json({
      message: `${action} successful`,
      stats: stats,
      downloadLink: `/download/${path.basename(outputPath)}`
    });
  });
};


app.post('/compress', upload.single('file'), (req, res) => {
  if (!req.file) return res.status(400).json({ error: "No file uploaded" });
  
  const inputPath = req.file.path;
  const outputPath = inputPath + ".huff"; 

  runHuffmanTool('compress', inputPath, outputPath, res);
});

app.post('/decompress', upload.single('file'), (req, res) => {
  if (!req.file) return res.status(400).json({ error: "No file uploaded" });

  const inputPath = req.file.path;
  let outputName = req.file.originalname.replace('.huff', '').replace('.bin', '');
  if (outputName === req.file.originalname) outputName += ".decompressed"; 
  
  const outputPath = path.join(os.tmpdir(), "decomp_" + Date.now() + "_" + outputName);

  runHuffmanTool('decompress', inputPath, outputPath, res);
});

app.get('/download/:filename', (req, res) => {
  const filename = path.basename(req.params.filename);
  const file = path.join(os.tmpdir(), filename);

  if (!fs.existsSync(file)) {
    return res.status(404).json({ error: "File expired or not found" });
  }

  res.download(file, (err) => {
    if (err) console.log("Download error:", err);
  });
});


app.listen(PORT, () => {
  console.log(`Server running on port ${PORT}`);
  cleanOldFiles();
});