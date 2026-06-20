# Text Compressor Backend 🚀

This is the backend service for the Text Compressor application. It is built with Node.js and Express, and it interfaces with a highly optimized C++ engine to perform advanced Huffman encoding and decoding. 

The backend exposes a REST API for file uploads, compression, decompression, and secure file retrieval. It is designed to be horizontally scalable and can run behind an Nginx load balancer.

## Architecture

* **Node.js/Express:** Handles HTTP requests, CORS, and multipart file uploads via `multer`.
* **C++ Engine:** A custom-built C++ executable (`huffman_tool`) is spawned as a child process to perform CPU-intensive compression tasks, preventing the Node.js event loop from blocking.
* **Temporary Storage:** Files are temporarily stored during processing and automatically cleaned up via a scheduled cleanup routine to prevent storage leaks.

## Prerequisites

* **Node.js** (v18+ recommended)
* **g++ Compiler** (Supports C++17)

## Local Development Setup

1. **Install Dependencies:**

   ```bash
   npm install
   ```

2. **Compile the C++ Engine:**

   ```bash
   npm run build
   ```
  
   *(This executes `g++ -std=c++17 huffman_advanced.cpp -o huffman_tool`)*

3. **Configure Environment Variables:**
   Create a `.env` file in the root directory:
  
   ```env
   PORT=3001
   FRONTEND_URL=http://localhost:5173
   ```

4. **Start the Server:**

   ```bash
   npm start
   ```

## API Documentation

### 1. Compress File

Compresses a given text file using the Huffman algorithm.

* **Endpoint:** `POST /compress`
* **Content-Type:** `multipart/form-data`
* **Parameters:**
  * `file` (Required): The text file to be compressed.
  * `rle` (Optional): Set to `'true'` to enable Run-Length Encoding as a pre-processing step.
* **Response (Success):** `200 OK`

  ```json
  {
    "message": "compress successful",
    "stats": {
      "originalSize": 1024,
      "compressedSize": 512,
      "compressionRatio": "50.00%"
    },
    "downloadLink": "/download/huff_1678888_file.txt.huff"
  }
  ```

### 2. Decompress File

Decompresses a `.huff` file back to its original text format.

* **Endpoint:** `POST /decompress`
* **Content-Type:** `multipart/form-data`
* **Parameters:**
  * `file` (Required): The `.huff` file to be decompressed.
* **Response (Success):** `200 OK`

  ```json
  {
    "message": "decompress successful",
    "stats": {
      "decompressedSize": 1024
    },
    "downloadLink": "/download/decomp_1678888_file.txt"
  }
  ```

### 3. Download File

Retrieves a processed file. Files expire and are deleted automatically 15 minutes after processing, or via the hourly cleanup job.

* **Endpoint:** `GET /download/:filename`
* **Response:** File stream (`application/octet-stream`).
* **Error:** `404 Not Found` if the file has expired.

## Testing the API (cURL)

**Test Compression:**

```bash
curl -X POST -F "file=@sample.txt" http://localhost:3001/compress
```

**Test Decompression:**

```bash
curl -X POST -F "file=@sample.txt.huff" http://localhost:3001/decompress
```

## Production Deployment

This backend is designed to be deployed on platforms like Render or Heroku.

* **Build Command:** `npm install && npm run build`
* **Start Command:** `npm start`
* **Scaling:** Deploy multiple instances of this backend and place them behind the Nginx Load Balancer for high availability.
