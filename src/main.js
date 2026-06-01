import './style.css';

import { initializeApp } from "firebase/app";
import { getDatabase, ref, set, onValue, update } from "firebase/database";

const firebaseConfig = {
  apiKey: import.meta.env.VITE_FIREBASE_API_KEY,
  authDomain: import.meta.env.VITE_FIREBASE_AUTH_DOMAIN,
  databaseURL: import.meta.env.VITE_FIREBASE_DATABASE_URL,
  projectId: import.meta.env.VITE_FIREBASE_PROJECT_ID,
  storageBucket: import.meta.env.VITE_FIREBASE_STORAGE_BUCKET,
  messagingSenderId: import.meta.env.VITE_FIREBASE_MESSAGING_SENDER_ID,
  appId: import.meta.env.VITE_FIREBASE_APP_ID
};

const app = initializeApp(firebaseConfig);
const db = getDatabase(app);

const BITMAP_WIDTH = 250;
const BITMAP_HEIGHT = 122;
const BITMAP_STRIDE = Math.ceil(BITMAP_WIDTH / 8);
const BITMAP_RAW_SIZE = BITMAP_STRIDE * BITMAP_HEIGHT;

const page = window.location.pathname;

if (page === "/receiver") {
  showReceiver();
} else {
  showSender();
}

function showSender() {
  document.querySelector('#app').innerHTML = `
    <main class="app sender">
      <section class="hero">
        <div class="otter">🦦</div>
        <p class="eyebrow">otter mail</p>
        <h1>Send a tiny note</h1>
        <p class="subtitle">A little message for the mailbox on her desk.</p>
      </section>

      <section class="quick-grid">
        <button id="thinking">thinking of you ❤️</button>
        <button id="love">i love you 💌</button>
        <button id="miss">i miss you 🥺</button>
        <button id="proud">proud of you ✨</button>
      </section>

      <section class="card">
        <label for="custom">custom note</label>
        <textarea id="custom" maxlength="120" placeholder="write something tiny..."></textarea>
        <button id="sendCustom" class="primary">send note</button>
        <p id="status" class="status"></p>
      </section>

      <section class="bitmap-preview" aria-label="bitmap message preview">
        <div class="preview-toolbar">
          <span>bitmap preview</span>
          <button id="previewBitmap" type="button">preview</button>
        </div>
        <canvas id="bitmapCanvas" width="250" height="122"></canvas>
        <p id="bitmapInfo" class="bitmap-info"></p>
      </section>

      <a class="tiny-link" href="/receiver">open receiver preview</a>
    </main>
  `;

  function sendMessage(message) {
    set(ref(db, "mailbox"), {
      message,
      flagUp: true,
      opened: false,
      sentAt: Date.now()
    });

    document.querySelector('#status').textContent = "sent to the tiny mailbox 🦦";
    setTimeout(() => {
      const status = document.querySelector('#status');
      if (status) status.textContent = "";
    }, 1800);
  }

  const input = document.querySelector('#custom');
  const previewButton = document.querySelector('#previewBitmap');

  function updateBitmapPreview(message) {
    const msg = typeof message === "string"
      ? message
      : input.value.trim() || "write something tiny...";
    const canvas = document.querySelector('#bitmapCanvas');
    renderBitmapPreview(canvas, msg);
  }

  function sendPresetMessage(message) {
    updateBitmapPreview(message);
    sendMessage(message);
  }

  input.addEventListener('input', updateBitmapPreview);
  previewButton.onclick = updateBitmapPreview;
  updateBitmapPreview();

  document.querySelector('#thinking').onclick = () => sendPresetMessage("thinking of you ❤️");
  document.querySelector('#love').onclick = () => sendPresetMessage("i love you 💌");
  document.querySelector('#miss').onclick = () => sendPresetMessage("i miss you 🥺");
  document.querySelector('#proud').onclick = () => sendPresetMessage("proud of you ✨");

  document.querySelector('#sendCustom').onclick = () => {
    const msg = input.value.trim();

    if (msg) {
      sendMessage(msg);
      input.value = "";
    }
  };
}

function renderBitmapPreview(canvas, message) {
  const ctx = canvas.getContext('2d');

  ctx.fillStyle = 'white';
  ctx.fillRect(0, 0, BITMAP_WIDTH, BITMAP_HEIGHT);

  ctx.fillStyle = 'black';
  ctx.font = 'bold 13px Arial, sans-serif';
  ctx.textBaseline = 'alphabetic';
  ctx.fillText('Otter Mail', 8, 20);
  ctx.fillRect(0, 28, BITMAP_WIDTH, 1);

  ctx.font = 'bold 18px Arial, "Segoe UI Emoji", "Apple Color Emoji", "Noto Color Emoji", sans-serif';
  drawWrappedCanvasText(ctx, message, 8, 55, 234, 22, 116);

  const packed = thresholdAndPackCanvas(canvas, ctx);
  const base64 = bytesToBase64(packed);

  document.querySelector('#bitmapInfo').textContent =
    `raw ${packed.length}/${BITMAP_RAW_SIZE} bytes · base64 ${base64.length} chars · ${base64.slice(0, 40)}`;
}

function drawWrappedCanvasText(ctx, text, x, startY, maxWidth, lineHeight, maxY) {
  const graphemes = splitGraphemes(text);
  let line = '';
  let y = startY;

  for (const grapheme of graphemes) {
    if (grapheme === '\r') continue;

    if (grapheme === '\n') {
      ctx.fillText(line, x, y);
      line = '';
      y += lineHeight;
      if (y > maxY) return;
      continue;
    }

    const nextLine = line + grapheme;
    if (line && ctx.measureText(nextLine).width > maxWidth) {
      ctx.fillText(line.trimEnd(), x, y);
      line = grapheme.trimStart();
      y += lineHeight;
      if (y > maxY) return;
    } else {
      line = nextLine;
    }
  }

  if (line && y <= maxY) {
    ctx.fillText(line.trimEnd(), x, y);
  }
}

function splitGraphemes(text) {
  if ('Segmenter' in Intl) {
    const segmenter = new Intl.Segmenter(undefined, { granularity: 'grapheme' });
    return [...segmenter.segment(text)].map((part) => part.segment);
  }

  return Array.from(text);
}

function thresholdAndPackCanvas(canvas, ctx) {
  const image = ctx.getImageData(0, 0, BITMAP_WIDTH, BITMAP_HEIGHT);
  const pixels = image.data;
  const packed = new Uint8Array(BITMAP_RAW_SIZE);

  for (let y = 0; y < BITMAP_HEIGHT; y++) {
    for (let x = 0; x < BITMAP_WIDTH; x++) {
      const pixelIndex = (y * BITMAP_WIDTH + x) * 4;
      const luminance =
        (pixels[pixelIndex] * 0.2126) +
        (pixels[pixelIndex + 1] * 0.7152) +
        (pixels[pixelIndex + 2] * 0.0722);
      const isBlack = luminance < 160;
      const output = isBlack ? 0 : 255;

      pixels[pixelIndex] = output;
      pixels[pixelIndex + 1] = output;
      pixels[pixelIndex + 2] = output;
      pixels[pixelIndex + 3] = 255;

      if (isBlack) {
        const byteIndex = (y * BITMAP_STRIDE) + Math.floor(x / 8);
        packed[byteIndex] |= 0x80 >> (x % 8);
      }
    }
  }

  ctx.putImageData(image, 0, 0);
  return packed;
}

function bytesToBase64(bytes) {
  let binary = '';
  const chunkSize = 0x8000;

  for (let i = 0; i < bytes.length; i += chunkSize) {
    binary += String.fromCharCode(...bytes.subarray(i, i + chunkSize));
  }

  return btoa(binary);
}

function showReceiver() {
  document.querySelector('#app').innerHTML = `
    <main class="app receiver">
      <section class="mailbox-card">
        <div id="flag" class="flag">📭 Empty</div>
        <div class="screen">
          <p class="screen-label">latest otter mail</p>
          <p id="message" class="message">no messages yet</p>
          <p id="time" class="time"></p>
        </div>
        <button id="opened" class="primary">open mail</button>
      </section>

      <a class="tiny-link" href="/">open sender</a>
    </main>
  `;

  document.querySelector('#opened').onclick = () => {
    update(ref(db, "mailbox"), {
      flagUp: false,
      opened: true
    });
  };

  onValue(ref(db, "mailbox"), (snapshot) => {
    const data = snapshot.val();
    if (!data) return;

    document.querySelector("#message").textContent = data.message;

    document.querySelector("#flag").textContent = data.flagUp
      ? "📬 You've got otter mail ❤️"
      : "📭 Empty";

    document.querySelector("#time").textContent = data.sentAt
      ? new Date(data.sentAt).toLocaleTimeString()
      : "";
  });
}
