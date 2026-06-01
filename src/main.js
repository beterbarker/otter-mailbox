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

  document.querySelector('#thinking').onclick = () => sendMessage("thinking of you ❤️");
  document.querySelector('#love').onclick = () => sendMessage("i love you 💌");
  document.querySelector('#miss').onclick = () => sendMessage("i miss you 🥺");
  document.querySelector('#proud').onclick = () => sendMessage("proud of you always ✨");

  document.querySelector('#sendCustom').onclick = () => {
    const input = document.querySelector('#custom');
    const msg = input.value.trim();

    if (msg) {
      sendMessage(msg);
      input.value = "";
    }
  };
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