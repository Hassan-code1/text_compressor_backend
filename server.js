require('dotenv').config();
const express = require('express');
const multer = require('multer');
const { execFile } = require('child_process');
const path = require('path');
const fs = require('fs');
const cors = require('cors');
const os = require('os');
const zlib = require('zlib');

const app = express();

app.set('trust proxy', 1);

// --- PRODUCTION SETUP ---
const PORT = process.env.PORT || 3001;
// Use environment variable for frontend URL or fallback to localhost
const FRONTEND_URL = process.env.FRONTEND_URL;

app.use(cors({
  origin: process.env.FRONTEND_URL || 'http://localhost:5173',
  methods: ["POST", "GET"],
  credentials: true
}));

// --- CONFIGURATION ---
const TEMP_DIR = os.tmpdir();
const UPLOADS_DIR = path.join(__dirname, 'uploads');

// Ensure we handle the upload storage correctly
const storage = multer.diskStorage({
  destination: TEMP_DIR,
  filename: (req, file, cb) => cb(null, "huff_" + Date.now() + "_" + file.originalname)
});
const upload = multer({ storage: storage });

// --- 1. STARTUP CLEANUP FUNCTION (Defined BEFORE usage) ---
const cleanOldFiles = () => {
  console.log("🧹 Running Startup Cleanup...");

  // A. Clean System Temp Directory
  fs.readdir(TEMP_DIR, (err, files) => {
    if (err) return;
    files.forEach(file => {
      // Only verify huffman related files
      if (file.startsWith('huff_') || file.startsWith('decomp_')) {
        const filePath = path.join(TEMP_DIR, file);
        fs.stat(filePath, (err, stats) => {
          if (err) return;
          const now = Date.now();
          const oneHour = 60 * 60 * 1000;
          // Delete if older than 1 hour
          if (now - stats.mtimeMs > oneHour) {
            fs.unlink(filePath, () => { });
          }
        });
      }
    });
  });

  // B. Nuke the old 'uploads/' folder content if it exists
  if (fs.existsSync(UPLOADS_DIR)) {
    fs.readdir(UPLOADS_DIR, (err, files) => {
      if (err) return;
      files.forEach(file => {
        fs.unlink(path.join(UPLOADS_DIR, file), (err) => {
          if (!err) console.log(`Deleted old file: uploads/${file}`);
        });
      });
    });
  }
};

// --- Helper: Clean single file ---
const cleanupFile = (filePath) => {
  if (fs.existsSync(filePath)) {
    fs.unlink(filePath, (err) => {
      if (err) console.error(`Error deleting ${filePath}:`, err);
    });
  }
};

const runHuffmanTool = (action, inputPath, outputPath, res, extraArgs = [], gzipBuf = null) => {
  const binaryPath = './huffman_tool';

  execFile(binaryPath, [action, inputPath, outputPath, ...extraArgs], (error, stdout, stderr) => {
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
      try { stats = JSON.parse(jsonLine.replace('JSON_RESULT:', '')); } catch (e) { }
    }

    // Add gzip benchmark size if provided
    if (gzipBuf !== null) stats.gzipSize = gzipBuf;

    setTimeout(() => { cleanupFile(outputPath); }, 15 * 60 * 1000);

    res.json({
      message: `${action} successful`,
      stats: stats,
      downloadLink: `/download/${path.basename(outputPath)}`
    });
  });
};

//temp route logger
app.use((req, res, next) => {
  console.log('--- REQUEST ---');
  console.log('method:', req.method);
  console.log('url:', req.originalUrl);
  console.log('host:', req.headers.host);
  console.log('origin:', req.headers.origin);
  console.log('protocol:', req.protocol);
  console.log('x-forwarded-proto:', req.headers['x-forwarded-proto']);
  next();
});


// --- ROUTES ---

app.post('/compress', upload.single('file'), (req, res) => {
  if (!req.file) return res.status(400).json({ error: "No file uploaded" });
  const inputPath = req.file.path;
  const outputPath = inputPath + ".huff";
  const useRle = req.body?.rle === 'true';
  const extraArgs = useRle ? ['--rle'] : [];

  // Run gzip on the raw input for benchmarking, then launch Huffman
  const rawBuf = fs.readFileSync(inputPath);
  zlib.gzip(rawBuf, (err, gzipped) => {
    const gzipSize = err ? null : gzipped.length;
    runHuffmanTool('compress', inputPath, outputPath, res, extraArgs, gzipSize);
  });
});

app.post('/decompress', upload.single('file'), (req, res) => {
  if (!req.file) return res.status(400).json({ error: "No file uploaded" });
  const inputPath = req.file.path;
  let outputName = req.file.originalname.replace('.huff', '').replace('.bin', '');
  if (outputName === req.file.originalname) outputName += ".decompressed";
  const outputPath = path.join(TEMP_DIR, "decomp_" + Date.now() + "_" + outputName);
  runHuffmanTool('decompress', inputPath, outputPath, res);
});

app.get('/download/:filename', (req, res) => {
  const filename = path.basename(req.params.filename);
  const file = path.join(TEMP_DIR, filename);

  if (!fs.existsSync(file)) {
    return res.status(404).json({ error: "File expired or not found" });
  }
  res.download(file, filename, (err) => {
    if (err) {
      console.log("Download error:", err);
      cleanupFile(file);
    }
  });
});

//temp
app.get("/debug", (req, res) => {
  res.json({
    host: req.headers.host,
    origin: req.headers.origin,
    forwardedFor: req.headers["x-forwarded-for"],
    forwardedProto: req.headers["x-forwarded-proto"]
  });
});

app.get('/health', (req, res) => {
  res.status(200).json({
    status: 'healthy',
    service: 'huffman-backend',
    timestamp: new Date().toISOString(),
    uptime: process.uptime()
  });
});
//temp test
app.post('/test-post', (req, res) => {
  res.json({ ok: true });
});

app.listen(PORT, () => {
  console.log(`Server running on port ${PORT}`);
  cleanOldFiles();
});