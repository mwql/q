// ==============================================================================
// 1. استدعاء المكتبات الضرورية (Imports)
// ==============================================================================
// هنا نقوم باستدعاء الأدوات التي يحتاجها البوت ليعمل:
// - "Client": هو البوت نفسه.
// - "EmbedBuilder": أداة لتصميم الرسائل المزينة (الملونة).
// - "SlashCommandBuilder": لإنشاء أوامر السلاش (/).
require('dotenv').config();
const { Client, GatewayIntentBits, Partials, ActivityType, REST, Routes, SlashCommandBuilder, EmbedBuilder, PermissionFlagsBits, MessageFlags, ActionRowBuilder, ButtonBuilder, ButtonStyle } = require('discord.js');
const express = require('express');
const path = require('path');
const fs = require('fs');
const crypto = require('crypto');
const Ewelink = require('ewelink-api-next').default;
const app = express();
app.set('trust proxy', 1);
const port = process.env.PORT || 3000;

// Path to main portfolio website (mwq-dev.top) - Works whether index.js is in /discord or at repo root
const mainDir = fs.existsSync(path.resolve(__dirname, '../main'))
    ? path.resolve(__dirname, '../main')
    : path.resolve(__dirname, 'main');

// Path to discord dashboard HTML - Works whether index.js is in /discord or at repo root
const discordHtmlPath = fs.existsSync(path.join(__dirname, 'index.html'))
    ? path.join(__dirname, 'index.html')
    : path.join(__dirname, 'discord', 'index.html');

// --- نظام التقاط السجلات (Log Capture System) ---
let recentLogs = [];
const maxLogs = 50;

function addLog(msg, type = 'INFO') {
    const timestamp = new Date().toLocaleTimeString();
    recentLogs.push(`--[${timestamp}] [${type}] ${msg}`);
    if (recentLogs.length > maxLogs) recentLogs.shift();
}

// تخزين الدوال الأصلية لمنع التكرار اللانهائي
const originalLog = console.log;
const originalError = console.error;

console.log = (...args) => {
    addLog(args.join(' '), 'INFO');
    originalLog(...args);
};

console.error = (...args) => {
    addLog(args.join(' '), 'ERROR');
    originalError(...args);
};

// --- دالة تشغيل لوحة تحكم البوت (Dashboard /dis) ---
function renderDiscordDashboard(req, res) {
    try {
        const generalReady = generalClient ? (generalClient.isReady() || !!generalClient.user) : false;
        const conanReady = conanClient ? (conanClient.isReady() || !!conanClient.user) : false;
        const listReady = listClient ? (listClient.isReady() || !!listClient.user) : false;
        
        // Diagnostic log
        console.log(`[DASHBOARD] General: ${generalReady ? 'Online' : 'Offline'}, gen2 bot: ${conanReady ? 'Online' : 'Offline'}`);

        const generalStatus = generalReady ? 'Online' : 'Offline';
        const conanStatus = conanReady ? 'Online' : 'Offline';
        const listStatus = listReady ? 'Online' : 'Offline';
        
        console.log(`[DEBUG] General Status: ${generalStatus}, gen2 bot Status: ${conanStatus}`);
        
        const generalGuilds = generalClient?.guilds?.cache?.size || 0;
        const conanGuilds = conanClient?.guilds?.cache?.size || 0;
        const generalPing = Math.round(generalClient?.ws?.ping || 0);
        const conanPing = Math.round(conanClient?.ws?.ping || 0);
        const listGuilds = listClient?.guilds?.cache?.size || 0;
        const listPing = Math.round(listClient?.ws?.ping || 0);

        // قراءة ملف HTML الخاص بداشبورد الديسكورد
        let html = fs.readFileSync(discordHtmlPath, 'utf8');

        // استبدال المتغيرات في ملف HTML بالقيم الحقيقية
        html = html
            .replace(/{{GENERAL_STATUS_CLASS}}/g, generalStatus === 'Online' ? 'status-online' : 'status-offline')
            .replace(/{{GENERAL_PULSE}}/g, generalStatus === 'Online' ? 'pulse' : '')
            .replace(/{{GENERAL_STATUS_TEXT}}/g, generalStatus === 'Online' ? 'Online' : 'Offline')
            .replace(/{{GENERAL_GUILDS}}/g, generalGuilds)
            .replace(/{{GENERAL_PING}}/g, generalPing)
            .replace(/{{CONAN_STATUS_CLASS}}/g, conanStatus === 'Online' ? 'status-online' : 'status-offline')
            .replace(/{{CONAN_PULSE}}/g, conanStatus === 'Online' ? 'pulse' : '')
            .replace(/{{CONAN_STATUS_TEXT}}/g, conanStatus === 'Online' ? 'Online' : 'Offline')
            .replace(/{{CONAN_GUILDS}}/g, conanGuilds)
            .replace(/{{CONAN_PING}}/g, conanPing)
            .replace(/{{LIST_STATUS_CLASS}}/g, listStatus === 'Online' ? 'status-online' : 'status-offline')
            .replace(/{{LIST_PULSE}}/g, listStatus === 'Online' ? 'pulse' : '')
            .replace(/{{LIST_STATUS_TEXT}}/g, listStatus)
            .replace(/{{LIST_GUILDS}}/g, listGuilds)
            .replace(/{{LIST_PING}}/g, listPing)
            .replace(/{{SYSTEM_LOGS}}/g, recentLogs.join('\n') || 'No logs recorded yet...');

        res.send(html);
    } catch (err) {
        console.error('Error serving Discord Dashboard (/dis):', err);
        // في حال فشل قراءة الملف، نرسل رداً بسيطاً لضمان بقاء الخدمة تعمل
        res.status(500).send('Bot is online! (Dashboard UI Error)');
    }
}

// 1. الصفحة الرئيسية: mwq-dev.top (تعرض main/index.html)
app.get('/', (req, res) => {
    res.sendFile(path.join(mainDir, 'index.html'));
});

// 2. صفحة من أنا: mwq-dev.top/about (تعرض main/about.html)
app.get(['/about', '/about.html'], (req, res) => {
    res.sendFile(path.join(mainDir, 'about.html'));
});

// 3. صفحة داشبورد بوت الديسكورد: mwq-dev.top/dis
app.get(['/dis', '/dis/', '/discord'], renderDiscordDashboard);

// 4. صفحة منبه وجدول النوم: mwq-dev.top/sleep
app.get(['/sleep', '/sleep.html'], (req, res) => {
    const sleepPath = fs.existsSync(path.join(mainDir, 'sleep.html'))
        ? path.join(mainDir, 'sleep.html')
        : path.join(__dirname, 'sleep.html');
    res.sendFile(sleepPath);
});

// إندبوينت لجلب السجلات فقط (للتحديث التلقائي)
app.get('/api/logs', (req, res) => {
    res.send(recentLogs.join('\n') || 'No logs recorded yet...');
});

app.use(express.json());

app.post('/api/bot/action', async (req, res) => {
    const { bot, action } = req.body;
    
    let client;
    let token;
    let name;

    if (bot === 'general') {
        client = generalClient;
        token = process.env.GENERAL_BOT_TOKEN || config.generalBot?.token;
        name = 'General Assistant';
    } else if (bot === 'conan') {
        client = conanClient;
        token = process.env.CONAN_BOT_TOKEN || config.conanBot?.token;
        name = 'gen2 bot';
    } else if (bot === 'lists') {
        client = listClient;
        token = process.env.LIST_BOT_TOKEN;
        name = 'Watch List bot';
    } else {
        return res.status(400).json({ message: 'Unknown bot' });
    }

    try {
        if (action === 'start') {
            console.log(`[UI] Starting ${name}...`);

            if (!token) {
                throw new Error(`${name} token is missing. Add its token to the Render environment variables.`);
            }
            
            // Create a new client after a stop so it has a clean Discord connection.
            if (bot === 'general') setupGeneralBot();
            else if (bot === 'conan') setupConanBot();
            else if (bot === 'lists') setupListBot();

            const targetClient = bot === 'general' ? generalClient : (bot === 'conan' ? conanClient : listClient);

            // discord.js v14 emits `clientReady` (not the old `ready` event).
            // Listen before logging in so the dashboard never misses the event.
            const ready = new Promise((resolve, reject) => {
                const timeout = setTimeout(() => {
                    cleanup();
                    reject(new Error(`${name} took too long to become ready.`));
                }, 25000);
                const cleanup = () => {
                    clearTimeout(timeout);
                    targetClient.off('clientReady', onReady);
                    targetClient.off('error', onError);
                };
                const onReady = () => {
                    cleanup();
                    resolve();
                };
                const onError = (error) => {
                    cleanup();
                    reject(error);
                };
                targetClient.once('clientReady', onReady);
                targetClient.once('error', onError);
            });

            // Handle the login and readiness promises together. This prevents a
            // readiness timeout from becoming an unhandled rejection while
            // login is still pending.
            await Promise.all([targetClient.login(token), ready]);

            res.send({ message: `${name} started successfully.` });
        } else if (action === 'stop') {
            console.log(`[UI] Stopping ${name}...`);
            
            // Clear polling for Conan if it's being stopped
            if (bot === 'conan' && conanPollingInterval) {
                clearInterval(conanPollingInterval);
                conanPollingInterval = null;
                console.log('[SYSTEM] gen2 bot polling stopped.');
            }

            // Reset the global variables to null so the dashboard sees them as Offline
            if (bot === 'general') {
                if (generalClient) generalClient.destroy();
                generalClient = null;
            } else if (bot === 'conan') {
                if (conanClient) conanClient.destroy();
                conanClient = null;
            } else if (bot === 'lists') {
                if (listClient) listClient.destroy();
                listClient = null;
            }
            
            // Wait a small amount for state to sync
            await new Promise(r => setTimeout(r, 1000));
            
            res.send({ message: `${name} stopped successfully.` });
        } else {
            res.status(400).json({ message: 'Unknown action' });
        }
    } catch (err) {
        console.error(`Error performing ${action} on ${bot}:`, err);
        res.status(500).json({ message: `Error: ${err.message}` });
    }
});

// --- iOS Custom Widget API (Scriptable / Widgy / Shortcuts) ---
app.get('/api/status/light', async (req, res) => {
    try {
        if (!ewelinkConnection || ewelinkDevices.length === 0) return res.send('OFF');
        const response = await manualFetchDevices(ewelinkConnection.region, ewelinkConnection.at);
        const list = response?.data?.thingList || [];
        const status = (list[0]?.itemData?.params?.switch || list[0]?.params?.switch || 'off').toUpperCase();
        res.send(status); 
    } catch (err) {
        res.send('OFF');
    }
});

app.get('/api/widget/status', async (req, res) => {
    const secret = process.env.IOS_WIDGET_SECRET || 'scriptable123';
    if (req.query.key !== secret) return res.status(403).json({ error: 'Unauthorized' });

    try {
        if (!ewelinkConnection) return res.status(500).json({ error: 'eWeLink not connected' });
        
        // Fetch live status
        const response = await manualFetchDevices(ewelinkConnection.region, ewelinkConnection.at);
        if (response && response.error && response.error !== 0) return res.status(500).json({ error: 'Fetch failed' });
        
        const list = response?.data?.thingList || [];
        let anyOn = false;
        
        list.forEach(item => {
            const data = item.itemData || item;
            if (data.params && data.params.switch === 'on') anyOn = true;
        });

        res.json({ status: anyOn ? 'on' : 'off' });
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

app.get('/api/widget/toggle', async (req, res) => {
    const secret = process.env.IOS_WIDGET_SECRET || 'scriptable123';
    if (req.query.key !== secret) return res.status(403).json({ error: 'Unauthorized' });

    try {
        if (!ewelinkConnection) return res.status(500).json({ error: 'eWeLink not connected' });
        
        // 1. Fetch live status to know whether to turn ON or OFF
        const response = await manualFetchDevices(ewelinkConnection.region, ewelinkConnection.at);
        const list = response?.data?.thingList || [];
        let anyOn = false;
        list.forEach(item => {
            const data = item.itemData || item;
            if (data.params && data.params.switch === 'on') anyOn = true;
        });

        const targetAction = anyOn ? 'off' : 'on';
        
        // 2. Toggle all devices
        for (const device of ewelinkDevices) {
            const deviceId = device.itemData?.deviceid || device.deviceid;
            await manualSetDeviceStatus(ewelinkConnection.region, ewelinkConnection.at, deviceId, targetAction);
        }

        res.json({ success: true, newStatus: targetAction });
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

// مكتبة التعامل مع خدمات جوجل (Google Sheets)
const { google } = require('googleapis');

// تحميل الإعدادات: نبحث أولاً في البيئة (Environment) ثم في الملف المحلي
let config = {};
if (fs.existsSync('./config.json')) {
    try {
        config = require('./config.json');
        console.log('-------------------------------------------');
        console.log('🚀 [SYSTEM] Starting in LOCAL mode');
        console.log('📂 Loading secrets from config.json and credentials.json');
        console.log('-------------------------------------------');
    } catch (e) {
        console.error('Error loading config.json:', e);
    }
} else {
    console.log('-------------------------------------------');
    console.log('☁️ [SYSTEM] Starting in CLOUD mode (Render)');
    console.log('🔑 Loading secrets from Environment Variables');
    console.log('-------------------------------------------');
}

// دالة مساعدة لجلب الكريدنشال من البيئة أو الملف
function getGoogleAuth(scopes) {
    let authOptions = { scopes };
    if (process.env.GOOGLE_CREDENTIALS) {
        try {
            authOptions.credentials = JSON.parse(process.env.GOOGLE_CREDENTIALS);
        } catch (e) {
            console.error('Failed to parse GOOGLE_CREDENTIALS env var:', e);
            if (fs.existsSync('./credentials.json')) authOptions.keyFile = path.join(__dirname, 'credentials.json');
        }
    } else if (fs.existsSync('./credentials.json')) {
        authOptions.keyFile = path.join(__dirname, 'credentials.json');
    }
    return new google.auth.GoogleAuth(authOptions);
}

// ==============================================================================
// 2. إعداد البوتات (Clients Setup)
// ==============================================================================

// +++ MANUAL EWELINK HELPERS (Bypassing the library) +++
function generateEwelinkSign(data, secret) {
    return crypto.createHmac('sha256', secret).update(data).digest('hex');
}

async function manualFetchDevices(region, at) {
    const appId = process.env.EWELINK_APP_ID;
    const timestamp = Date.now();
    const nonce = Math.random().toString(36).substring(2, 10);
    
    // Strict Bearer token cleaning
    const cleanAt = String(at).replace('Bearer ', '').trim().toLowerCase();
    
    // Official V2 API List Things
    const url = `https://${region}-apia.coolkit.cc/v2/device/thing`;

    try {
        const fetch = globalThis.fetch || (await import('node-fetch')).default;
        const response = await fetch(url, {
            headers: {
                'Authorization': `Bearer ${cleanAt}`,
                'X-CK-Appid': appId,
                'X-CK-Nonce': nonce,
                'X-CK-Timestamp': timestamp.toString(),
                'Content-Type': 'application/json'
            }
        });
        return await response.json();
    } catch (err) {
        console.error('[MANUAL FETCH] Error:', err.message);
        return { error: 500, msg: err.message };
    }
}

async function manualSetDeviceStatus(region, at, deviceid, action) {
    const appId = process.env.EWELINK_APP_ID;
    const timestamp = Date.now();
    const nonce = Math.random().toString(36).substring(2, 10);
    
    // Strict Bearer token cleaning
    const cleanAt = String(at).replace('Bearer ', '').trim().toLowerCase();
    
    const url = `https://${region}-apia.coolkit.cc/v2/device/thing/status`;
    const body = JSON.stringify({
        type: 1,
        id: deviceid,
        params: { switch: action }
    });

    try {
        const fetch = globalThis.fetch || (await import('node-fetch')).default;
        const response = await fetch(url, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Authorization': `Bearer ${cleanAt}`,
                'X-CK-Appid': appId,
                'X-CK-Nonce': nonce,
                'X-CK-Timestamp': timestamp.toString()
            },
            body: body
        });
        return await response.json();
    } catch (err) {
        console.error('[MANUAL ACTION] Error:', err.message);
        return { error: 500, msg: err.message };
    }
}

// --- إعداد eWeLink (Sonoff) ---
let ewelinkConnection = null;
let ewelinkDevices = [];

async function initEwelink(directTokenData = null) {
    const appId = process.env.EWELINK_APP_ID;
    const appSecret = process.env.EWELINK_APP_SECRET;

    if (!appId || !appSecret) {
        console.warn('⚠️ [EWELINK] Missing EWELINK_APP_ID or EWELINK_APP_SECRET. Ewelink disabled.');
        return;
    }

    try {
        console.log(`🔍 [EWELINK] Starting manual initialization...`);
        let tokenData = directTokenData;
        
        if (!tokenData && fs.existsSync('./ewelink_token.json')) {
            tokenData = JSON.parse(fs.readFileSync('./ewelink_token.json', 'utf8'));
        }

        if (!tokenData || !tokenData.at) {
             console.log(`⚠️ [EWELINK] No Access Token found. Please login via the Web Dashboard.`);
             return;
        }

        const region = tokenData.region || process.env.EWELINK_REGION || 'eu';
        const at = String(tokenData.at).trim().toLowerCase();

        console.log(`📡 [MANUAL FETCH] Attempting to fetch devices for region: "${region}"...`);
        const response = await manualFetchDevices(region, at);
        
        if (response && response.error && response.error !== 0) {
             console.error(`❌ [MANUAL FETCH] Failed (Error ${response.error}):`, response.msg);
             return;
        }

        const list = response?.data?.thingList || [];
        
        // We'll store the connection info for our manual actions
        ewelinkConnection = { manual: true, at, region }; 

        ewelinkDevices = list.map(item => {
            if (item.itemData && item.itemData.deviceid) return item;
            if (item.deviceid) return { itemData: item };
            return null;
        }).filter(item => item !== null);

        console.log(`✅ [MANUAL FETCH] SUCCESS! Found ${ewelinkDevices.length} devices.`);
    } catch (err) {
        console.error('❌ [EWELINK] Manual Setup failed:', err.message);
    }
}

// Call init once at the start
initEwelink();

// إعداد البوت العام (General Bot)
let generalClient;

function attachDiscordConnectionDiagnostics(client, name) {
    client.on('error', error => console.error(`[DISCORD] ${name} client error:`, error));
    client.on('shardError', (error, shardId) => console.error(`[DISCORD] ${name} gateway error (shard ${shardId}):`, error));
    client.on('shardDisconnect', (event, shardId) => {
        console.error(`[DISCORD] ${name} gateway disconnected (shard ${shardId}, code ${event.code}): ${event.reason || 'No reason provided'}`);
    });
    client.on('invalidated', () => console.error(`[DISCORD] ${name} session was invalidated.`));
}

function setupGeneralBot() {
    if (generalClient) {
        try { generalClient.destroy(); } catch (e) { console.log('Error destroying old General client:', e.message); }
    }
    
    generalClient = new Client({
        intents: [
            GatewayIntentBits.Guilds,
            GatewayIntentBits.GuildMessages,
            GatewayIntentBits.MessageContent,
        ],
        partials: [Partials.Channel, Partials.Message],
    });
    attachDiscordConnectionDiagnostics(generalClient, 'General Assistant');

    // --- الأحداث الخاصة بالبوت العام ---
    generalClient.once('clientReady', () => {
        loadAutoReplies();
        console.log('--------------------------------------');
        console.log(`📡 General Bot is now ONLINE!`);
        console.log(`👤 Logged in as: ${generalClient.user.tag}`);
        console.log(`💬 ${autoRepliesMap.size} auto-replies loaded`);
        console.log('--------------------------------------');
        registerGeneralCommands();
    });

    // تسجيل مجمع للأحداث لتسهيل إعادة التشغيل
    attachGeneralInteractionHandler();
    attachGeneralMessageHandler();
}

// إعداد بوت كونان (Conan Bot)
let conanClient;

function setupConanBot() {
    if (conanClient) {
        try { conanClient.destroy(); } catch (e) { console.log('Error destroying old Conan client:', e.message); }
    }

    conanClient = new Client({
        intents: [
            GatewayIntentBits.Guilds,
            GatewayIntentBits.GuildMessages,
            GatewayIntentBits.MessageContent,
        ],
        partials: [Partials.Channel, Partials.Message],
    });
    attachDiscordConnectionDiagnostics(conanClient, 'gen2 bot');

    // --- الأحداث الخاصة ببوت كونان ---
    conanClient.once('clientReady', () => {
        console.log('--------------------------------------');
        console.log(`📡 gen2 bot is now ONLINE!`);
        console.log(`👤 Logged in as: ${conanClient.user.tag}`);
        console.log(`🏠 Serving ${conanClient.guilds.cache.size} servers`);
        console.log('✅ Status: features active');
        console.log('--------------------------------------');

        registerConanCommands();
        loadSheetState();
        checkDetailedChanges(); 

        if (conanPollingInterval) clearInterval(conanPollingInterval);
        conanPollingInterval = setInterval(checkDetailedChanges, config.POLL_INTERVAL || 60000);
    });

    attachConanInteractionHandler();
    attachConanMessageHandler();
}

// Watch List bot: a separate Discord bot for the Movies and Series tabs.
let listClient;

function setupListBot() {
    if (listClient) {
        try { listClient.destroy(); } catch (e) { console.log('Error destroying old Watch List client:', e.message); }
    }
    listClient = new Client({
        intents: [GatewayIntentBits.Guilds, GatewayIntentBits.GuildMessages, GatewayIntentBits.MessageContent],
        partials: [Partials.Channel, Partials.Message],
    });
    attachDiscordConnectionDiagnostics(listClient, 'Watch List');
    listClient.once('clientReady', () => {
        console.log(`📺 Watch List bot is ONLINE as ${listClient.user.tag}`);
        registerListCommands();
    });
    attachListInteractionHandler();
    attachListMessageHandler();
}

// ==============================================================================
// 3. إعدادات جوجل شيت (Google Sheets Config)
// ==============================================================================
// معرف جدول بيانات جوجل الخاص بك.
const SPREADSHEET_ID = process.env.SPREADSHEET_ID || config.SPREADSHEET_ID || '1w6V3qSCmODn63jT6pierR2C1Ous5pBfpAz7GfPvb5d4';

// اسم الورقة
const SHEET_NAME = process.env.SHEET_NAME || config.SHEET_NAME || 'Eps';

// الآي دي الخاص بالقناة المراقبة
const TARGET_CHANNEL_ID = process.env.TARGET_CHANNEL_ID || config.TARGET_CHANNEL_ID || '1483235450343653377';

// إعدادات صفحة الأفلام وقناة تسجيل المشاهدة. يمكن تغييرها من config.json أو متغيرات البيئة.
const MOVIES_SHEET_NAME = process.env.MOVIES_SHEET_NAME || config.MOVIES_SHEET_NAME || 'Movies';
const MOVIES_CHANNEL_ID = process.env.MOVIES_CHANNEL_ID || config.MOVIES_CHANNEL_ID || '1542797207403765790';
let resolvedMoviesSheetName = null;

// The channel IDs are deliberately kept here, as requested; only the token,
// client ID, and spreadsheet ID belong in Render's environment variables.
const LIST_SPREADSHEET_ID = process.env.LISTSHEET_ID;
const LIST_MOVIES_SHEET = 'Movies';
const LIST_SERIES_SHEET = 'Series';
const LIST_MOVIES_CHANNEL_ID = '1547898356699758662';
const LIST_SERIES_CHANNEL_ID = '1547898407618617414';

// ==============================================================================
// 4. نظام الردود التلقائية (Auto Replies System)
// ==============================================================================
// هذه المصفوفة أو القائمة ستحتفظ بالردود التلقائية في الذاكرة لتسريع قراءتها.
let autoRepliesMap = new Map();

// دالة (Function) لتحميل الردود من ملف auto_replies.json عند تشغيل البوت
function loadAutoReplies() {
    try {
        if (!fs.existsSync('./auto_replies.json')) {
            fs.writeFileSync('./auto_replies.json', '[]'); // إذا لم يكن الملف موجوداً، سيتم إنشاؤه كقائمة فارغة
        }
        const data = JSON.parse(fs.readFileSync('./auto_replies.json', 'utf8'));
        autoRepliesMap.clear();
        if (Array.isArray(data)) {
            data.forEach(item => {
                if (item.trigger && item.reply) {
                    autoRepliesMap.set(item.trigger, item.reply);
                }
            });
        }
        // طباعة عدد الردود التي تم تحميلها بنجاح في الشاشة (الكونسول)
        console.log(`Loaded ${autoRepliesMap.size} auto-replies.`);
    } catch (err) {
        console.error('Error loading auto-replies:', err);
    }
}

// دالة (Function) لحفظ الردود في ملف auto_replies.json بعد إضافة أو حذف رد
function saveAutoReplies() {
    try {
        const data = Array.from(autoRepliesMap.entries()).map(([trigger, reply]) => ({
            trigger,
            reply
        }));
        // حفظ الملف بتنسيق جميل ومرتب
        fs.writeFileSync('./auto_replies.json', JSON.stringify(data, null, 2), 'utf8');
        console.log('Saved auto-replies to file.');
    } catch (err) {
        console.error('Error saving auto-replies:', err);
    }
}

// ==============================================================================
// 5. نظام مراقبة تغييرات الجدول (Polling System)
// ==============================================================================
let lastSheetState = new Map(); // الذاكرة التي تحفظ حالة حلقات Eps فقط
let conanPollingInterval = null; // تخزين المؤقت لمنع التكرار

// دالة لاسترجاع آخر حالة محفوظة من الجدول لكي لا يرسل البوت تنبيهات مكررة عند إعادة التشغيل
function loadSheetState() {
    try {
        if (fs.existsSync('./sheet_state.json')) {
            const data = JSON.parse(fs.readFileSync('./sheet_state.json', 'utf8'));
            if (data.format === 'eps-v1' && data.episodes) {
                lastSheetState = new Map(Object.entries(data.episodes));
                console.log(`Loaded ${lastSheetState.size} episodes from history.`);
            }
        }
    } catch (err) {
        console.error('Error loading sheet state:', err);
    }
}

// دالة لحفظ حالة الجدول الحالية إلى الملف
function saveSheetState() {
    try {
        const data = { format: 'eps-v1', episodes: Object.fromEntries(lastSheetState) };
        fs.writeFileSync('./sheet_state.json', JSON.stringify(data, null, 2), 'utf8');
    } catch (err) {
        console.error('Error saving sheet state:', err);
    }
}

// تراقب تغييرات ورقة Eps فقط وترسلها إلى قناة السجل الأساسية.
async function checkDetailedChanges() {
    try {
        const auth = getGoogleAuth(['https://www.googleapis.com/auth/spreadsheets']);
        const sheets = google.sheets({ version: 'v4', auth });
        const response = await sheets.spreadsheets.values.get({
            spreadsheetId: SPREADSHEET_ID,
            range: `${SHEET_NAME}!A4:S500`,
        });
        const currentData = new Map();
        const rows = response.data.values || [];
        console.log(`Polling sheet: Found ${rows.length} rows.`);
        rows.forEach(row => {
            if (row[0] && row[0].toString().trim() !== '') {
                const ep = row[0].toString().trim();
                currentData.set(ep, {
                    highlights: row[2]?.toString().trim() || '',
                    rating: row[4]?.toString().trim() || '',
                    watched: row[8] === 'TRUE' || row[8] === true,
                });
            }
            if (row[18] && row[18].toString().trim() !== '') {
                const ep = row[18].toString().trim();
                currentData.set(ep, {
                    highlights: row[16]?.toString().trim() || '',
                    rating: row[14]?.toString().trim() || '',
                    watched: row[10] === 'TRUE' || row[10] === true,
                });
            }
        });

        if (lastSheetState.size === 0) {
            lastSheetState = currentData;
            saveSheetState();
            return;
        }

        const epsLogChannelId = process.env.LOG_CHANNEL_ID || config.LOG_CHANNEL_ID;
        const logChannel = epsLogChannelId ? await conanClient.channels.fetch(epsLogChannelId).catch(() => null) : null;
        if (!logChannel) return;

        for (const [ep, data] of currentData.entries()) {
            const previous = lastSheetState.get(ep);
            if (!previous) continue;
            const changes = [];
            if (data.watched !== previous.watched) {
                changes.push(`تم تغيير حالة مشاهدة الحلقة **${ep}** إلى: ${data.watched ? '✅ (تمت المشاهدة)' : '❌ (غير مشاهدة)'}`);
            }
            if (data.highlights !== previous.highlights) {
                changes.push(`تعديل في أحداث الحلقة **${ep}**:\n**من:** ${previous.highlights || '---'}\n**إلى:** ${data.highlights || '---'}`);
            }
            if (data.rating !== previous.rating) {
                changes.push(`تحديث تقييم الحلقة **${ep}**:\n**من:** ${previous.rating || '---'}\n**إلى:** ${data.rating || '---'}`);
            }
            if (changes.length === 0) continue;
            await logChannel.send({ embeds: [new EmbedBuilder()
                .setTitle(`📝 تغيير في الجدول (الحلقة ${ep})`)
                .setColor(0x2ECC71)
                .setDescription(changes.join('\n\n'))
                .setTimestamp(),
            ] });
        }

        lastSheetState = currentData;
        saveSheetState();

    } catch (err) {
        console.error('Error in checkDetailedChanges:', err);
    }
}

// ==============================================================================
// 6. تسجيل الأوامر (Slash Commands Registration)
// ==============================================================================

// تقوم هذه الدالة بتعريف أوامر البوت العام للديسكورد (إضافة الاسم، الوصف، والخيارات)
async function registerGeneralCommands() {
    const commands = [
        new SlashCommandBuilder()
            .setName('list_auto_replies') // اسم الأمر
            .setDescription('عرض قائمة الردود التلقائية المفعلة'), // الوصف: يظهر للمستخدم في الديسكورد
            
        new SlashCommandBuilder()
            .setName('add_reply')
            .setDescription('إضافة رد تلقائي جديد (للمشرفين فقط)')
            .setDefaultMemberPermissions(PermissionFlagsBits.Administrator) // هذا الخيار يمنع التفعيل لغير المدراء
            .addStringOption(option => // إضافة حقل إدخال المستطيل الأول
                option.setName('trigger')
                    .setDescription('الكلمة التي تثير الرد')
                    .setRequired(true)) // Required(true) تعني إجباري
            .addStringOption(option => // إضافة حقل إدخال المستطيل الثاني
                option.setName('reply')
                    .setDescription('الرد الذي سيقوم به البوت')
                    .setRequired(true)),
                    
        new SlashCommandBuilder()
            .setName('remove_reply')
            .setDescription('حذف رد تلقائي')
            .setDefaultMemberPermissions(PermissionFlagsBits.Administrator)
            .addStringOption(option =>
                option.setName('trigger')
                    .setDescription('الكلمة التي تريد حذف الرد الخاص بها')
                    .setRequired(true)),
                    
        new SlashCommandBuilder()
            .setName('dm')
            .setDescription('إرسال رسالة خاصة لمستخدم (للمشرفين فقط)')
            .setDefaultMemberPermissions(PermissionFlagsBits.Administrator)
            .addUserOption(option => // UserOption يطلب من المستخدم اختيار شخص أخر بطريقة المنشن
                option.setName('user')
                    .setDescription('المستخدم الذي تريد مراسلته')
                    .setRequired(true))
            .addStringOption(option =>
                option.setName('message')
                    .setDescription('محتوى الرسالة')
                    .setRequired(true)),
                    
        new SlashCommandBuilder()
            .setName('send')
            .setDescription('يجعل البوت يرسل رسالة من اختيارك في المحادثة')
            .setDefaultMemberPermissions(PermissionFlagsBits.Administrator)
            .addStringOption(option =>
                option.setName('message')
                    .setDescription('الرسالة التي سيقولها البوت')
                    .setRequired(true)),

        new SlashCommandBuilder()
            .setName('clear')
            .setDescription('حذف عدد معين من الرسائل (للمشرفين فقط)')
            .setDefaultMemberPermissions(PermissionFlagsBits.Administrator)
            .addIntegerOption(option =>
                option.setName('number')
                    .setDescription('عدد الرسائل المراد حذفها')
                    .setRequired(true)
                    .setMinValue(1)
                    .setMaxValue(100)),
                    
        new SlashCommandBuilder()
            .setName('role')
            .setDescription('إعطاء رتبة لمستخدم (للمشرفين فقط)')
            .setDefaultMemberPermissions(PermissionFlagsBits.Administrator)
            .addUserOption(option => 
                option.setName('user')
                    .setDescription('المستخدم الذي تريد إعطاءه الرتبة')
                    .setRequired(true))
            .addRoleOption(option => 
                option.setName('role')
                    .setDescription('الرتبة التي تريد إعطاءها')
                    .setRequired(true)),

        new SlashCommandBuilder()
            .setName('devices')
            .setDescription('عرض قائمة بأجهزة Sonoff (eWeLink) وحالتها الحالية'),
            
        new SlashCommandBuilder()
            .setName('on_all')
            .setDescription('تشغيل جميع الأجهزة المتصلة')
            .setDefaultMemberPermissions(PermissionFlagsBits.Administrator),
            
        new SlashCommandBuilder()
            .setName('off_all')
            .setDescription('إطفاء جميع الأجهزة المتصلة')
            .setDefaultMemberPermissions(PermissionFlagsBits.Administrator),

        new SlashCommandBuilder()
            .setName('turn')
            .setDescription('تبويب (Toggle) جميع الأجهزة المتصلة')
            .setDefaultMemberPermissions(PermissionFlagsBits.Administrator),
    ].map(command => command.toJSON()); // تحويل البيانات للصيغة التي يفهمها سيرفر الديسكورد

    const generalToken = process.env.GENERAL_BOT_TOKEN || config.generalBot?.token;
    const generalClientId = process.env.GENERAL_BOT_CLIENT_ID || config.generalBot?.clientId;
    if (!generalToken) return console.error('[ERROR] GENERAL_BOT_TOKEN is missing!');
    const rest = new REST({ version: '10' }).setToken(generalToken);

    try {
        console.log(`[DEBUG] Registering General commands for Client ID: ${generalClientId}`);
        await rest.put(
            Routes.applicationCommands(generalClientId),
            { body: commands },
        );
        console.log('Successfully reloaded General Bot commands globally.');
    } catch (error) {
        console.error('Error registering General Bot commands:', error);
    }
}

// نفس فكرة الدالة السابقة، ولكن لتسجيل الأوامر الخاصة ببوت كونان
async function registerConanCommands() {
    const commands = [
        new SlashCommandBuilder()
            .setName('add_episode')
            .setDescription('إضافة حلقة جديدة إلى جدول كونان')
            .addStringOption(option =>
                option.setName('episode')
                    .setDescription('رقم الحلقة (مثال: 43 أو 123-124)')
                    .setRequired(true))
            .addStringOption(option =>
                option.setName('highlights')
                    .setDescription('أبرز الأحداث للحلقة (اختياري)')
                    .setRequired(false)),
        new SlashCommandBuilder()
            .setName('remove_episode')
            .setDescription('حذف حلقة من جدول كونان')
            .addStringOption(option =>
                option.setName('episode')
                    .setDescription('رقم الحلقة المراد حذفها')
                    .setRequired(true)),
        new SlashCommandBuilder()
            .setName('unwatched')
            .setDescription('عرض قائمة الحلقات غير المشاهدة'),
        new SlashCommandBuilder()
            .setName('all')
            .setDescription('عرض قائمة جميع الحلقات المتوفرة'),
        new SlashCommandBuilder()
            .setName('wiki')
            .setDescription('الحصول على رابط صفحة الويكي الخاصة بالحلقة')
            .addStringOption(option =>
                option.setName('episode')
                    .setDescription('رقم الحلقة (مثال: 123 أو 123-124)')
                    .setRequired(true)),
        new SlashCommandBuilder()
            .setName('commands')
            .setDescription('عرض قائمة بجميع أوامر البوت وشرح مميزاته'),
        new SlashCommandBuilder()
            .setName('episode_info')
            .setDescription('الحصول على معلومات مفصلة عن حلقة معينة')
            .addStringOption(option =>
                option.setName('episode')
                    .setDescription('رقم الحلقة (مثال: 129)')
                    .setRequired(true)),
        new SlashCommandBuilder()
            .setName('add_highlights')
            .setDescription('تحديث أبرز أحداث حلقة معينة')
            .addStringOption(option =>
                option.setName('episode')
                    .setDescription('رقم الحلقة (مثال: 129)')
                    .setRequired(true))
            .addStringOption(option =>
                option.setName('text')
                    .setDescription('النص الجديد للأحداث')
                    .setRequired(true)),
        new SlashCommandBuilder()
            .setName('movie_highlights')
            .setDescription('تحديث أبرز أحداث فيلم في جدول Movies')
            .addStringOption(option =>
                option.setName('movie')
                    .setDescription('رقم الفيلم')
                    .setRequired(true))
            .addStringOption(option =>
                option.setName('text')
                    .setDescription('النص الجديد للأحداث')
                    .setRequired(true)),
        new SlashCommandBuilder()
            .setName('all_m')
            .setDescription('عرض جميع الأفلام مع أبرز الأحداث وعدد مرات التكرار'),
        new SlashCommandBuilder()
            .setName('sort_episodes')
            .setDescription('ترتيب الحلقات في الجدول تصاعدياً حسب الرقم'),
        new SlashCommandBuilder()
            .setName('the_code')
            .setDescription('إرسال ملف الكود المصدري للبوت كاملاً'),
    ].map(command => command.toJSON());

    const conanToken = process.env.CONAN_BOT_TOKEN || config.conanBot?.token;
    const conanClientId = process.env.CONAN_BOT_CLIENT_ID || config.conanBot?.clientId;
    const rest = new REST({ version: '10' }).setToken(conanToken);

    try {
        console.log(`[DEBUG] Registering Conan commands for Client ID: ${conanClientId}`);
        await rest.put(
            Routes.applicationCommands(conanClientId),
            { body: commands },
        );
        console.log('Successfully reloaded Conan Bot commands globally.');
    } catch (error) {
        console.error('Error registering Conan Bot commands:', error);
    }
}

// ==============================================================================
// 7. وظائف أدوات الجدول (Google Sheets Utils Functions)
// ==============================================================================

// هذه الدالة المسؤولة عن تفعيل/إلغاء الصح على حلقة معينة في الجدول (عند كتابة الرقم في القناة المخصصة)
async function updateConanSheet(episodeNumber) {
    const auth = getGoogleAuth(['https://www.googleapis.com/auth/spreadsheets']);
    const sheets = google.sheets({ version: 'v4', auth });

    const ep = episodeNumber.toString().trim();

    // جلب البيانات (A=الحلقات يسار، K=الشيك بوكس، S=الحلقات يمين)
    const response = await sheets.spreadsheets.values.get({
        spreadsheetId: SPREADSHEET_ID,
        range: `${SHEET_NAME}!A4:S500`,
    });

    const rows = response.data.values;
    if (!rows) throw new Error('الجدول فارغ!');

    let foundRow = -1;
    let checkCol = 'K'; // العمود الافتراضي للصف الذي يحتوي على مربع الصح

    // البحث في العمود A (الجهة اليسرى) واالعمود S (الجهة اليمنى)
    for (let i = 0; i < rows.length; i++) {
        const row = rows[i];
        // العمود A يقابله بالاندكس رقم 0 والعمود I رقمه 8 (ولكن كحرف في التحديث I)
        if (row[0] && row[0].toString().trim() === ep) {
            foundRow = i;
            checkCol = 'I'; // تحديث: مكان عمود الصح الخاص بالجانب الأيسر
            break;
        }
        // العمود S = index 18
        if (row[18] && row[18].toString().trim() === ep) {
            foundRow = i;
            checkCol = 'K'; // تحديث: مكان عمود الصح الخاص بالجانب الأيمن
            break;
        }
    }

    if (foundRow === -1) throw new Error(`الحلقة ${ep} غير موجودة.`);

    const actualRow = foundRow + 4; // لأننا بدأنا الجلب من رقم 4

    // قراءة القيمة الحالية من عمود الشيك بوكس لنقوم بعكسها
    const currentRes = await sheets.spreadsheets.values.get({
        spreadsheetId: SPREADSHEET_ID,
        range: `${SHEET_NAME}!${checkCol}${actualRow}`,
    });

    // تحويل القيمة المقروءة إلى صحيح (true) أو خطأ (false)
    const currentVal = currentRes.data.values?.[0]?.[0];
    const isChecked = currentVal === true || currentVal === 'TRUE' || currentVal === 'TRUE';
    const newVal = !isChecked; // عكس القيمة !

    // التحديث الفعلي لمكان الصح في الجدول
    await sheets.spreadsheets.values.update({
        spreadsheetId: SPREADSHEET_ID,
        range: `${SHEET_NAME}!${checkCol}${actualRow}`,
        valueInputOption: 'USER_ENTERED', // لضمان إدخاله كاختيار صحيح وليس كنص
        resource: { values: [[newVal]] },
    });

    console.log(`✅ Toggled ${checkCol}${actualRow} from ${isChecked} to ${newVal} for episode ${ep}`);
}

// ترتيب عمليات الزيادة لكل فيلم يمنع ضياع زيادة إذا أرسلت رسالتان بسرعة.
const movieUpdateQueues = new Map();

function normalizeMovieNumber(value) {
    const arabicDigits = '٠١٢٣٤٥٦٧٨٩';
    return value.toString().trim().replace(/[٠-٩]/g, digit => arabicDigits.indexOf(digit).toString());
}

function quoteSheetName(sheetName) {
    return `'${sheetName.replace(/'/g, "''")}'`;
}

// يجلب الاسم الفعلي للورقة من Google Sheets؛ هذا يتفادى مشكلة اختلاف الحروف أو المسافات في اسم التبويب.
async function getMoviesSheetName(sheets) {
    if (resolvedMoviesSheetName) return resolvedMoviesSheetName;

    const metadata = await sheets.spreadsheets.get({
        spreadsheetId: SPREADSHEET_ID,
        fields: 'sheets.properties.title',
    });
    const sheetNames = (metadata.data.sheets || []).map(sheet => sheet.properties.title);
    const requestedName = MOVIES_SHEET_NAME.trim().toLocaleLowerCase();
    const matchingName = sheetNames.find(name => name.trim().toLocaleLowerCase() === requestedName);
    if (!matchingName) {
        throw new Error(`لم يتم العثور على ورقة الأفلام باسم "${MOVIES_SHEET_NAME}". الأوراق المتاحة: ${sheetNames.join(', ')}`);
    }
    resolvedMoviesSheetName = matchingName;
    return matchingName;
}

async function findMovieCell(movieNumber, valueColumn) {
    const auth = getGoogleAuth(['https://www.googleapis.com/auth/spreadsheets']);
    const sheets = google.sheets({ version: 'v4', auth });
    const movie = normalizeMovieNumber(movieNumber);
    const sheetName = await getMoviesSheetName(sheets);
    const response = await sheets.spreadsheets.values.get({
        spreadsheetId: SPREADSHEET_ID,
        range: `${quoteSheetName(sheetName)}!A4:S500`,
    });
    const rows = response.data.values || [];

    for (let i = 0; i < rows.length; i++) {
        const row = rows[i];
        const actualRow = i + 4;
        if (row[0] && normalizeMovieNumber(row[0]) === movie) {
            return { sheets, sheetName, movie, cell: `${valueColumn.left}${actualRow}` };
        }
        if (row[18] && normalizeMovieNumber(row[18]) === movie) {
            return { sheets, sheetName, movie, cell: `${valueColumn.right}${actualRow}` };
        }
    }
    throw new Error(`الفيلم ${movie} غير موجود في ورقة ${sheetName}.`);
}

async function incrementMovieRepeatCount(movieNumber) {
    const movie = normalizeMovieNumber(movieNumber);
    const previous = movieUpdateQueues.get(movie) || Promise.resolve();
    const update = previous.catch(() => {}).then(async () => {
        const found = await findMovieCell(movie, { left: 'I', right: 'K' });
        const current = await found.sheets.spreadsheets.values.get({
            spreadsheetId: SPREADSHEET_ID,
            range: `${quoteSheetName(found.sheetName)}!${found.cell}`,
        });
        const rawValue = current.data.values?.[0]?.[0];
        const count = Number.parseInt(rawValue, 10);
        const newCount = Number.isFinite(count) && count >= 0 ? count + 1 : 1;

        await found.sheets.spreadsheets.values.update({
            spreadsheetId: SPREADSHEET_ID,
            range: `${quoteSheetName(found.sheetName)}!${found.cell}`,
            valueInputOption: 'USER_ENTERED',
            resource: { values: [[newCount]] },
        });
        console.log(`✅ Incremented movie ${movie} repeat count to ${newCount} (${found.cell})`);
        return newCount;
    });
    movieUpdateQueues.set(movie, update);
    try {
        return await update;
    } finally {
        if (movieUpdateQueues.get(movie) === update) movieUpdateQueues.delete(movie);
    }
}

async function updateMovieHighlights(movieNumber, newText) {
    const found = await findMovieCell(movieNumber, { left: 'C', right: 'Q' });
    const current = await found.sheets.spreadsheets.values.get({
        spreadsheetId: SPREADSHEET_ID,
        range: `${quoteSheetName(found.sheetName)}!${found.cell}`,
    });
    const oldValue = current.data.values?.[0]?.[0] || 'لا يوجد نص سابق';
    await found.sheets.spreadsheets.values.update({
        spreadsheetId: SPREADSHEET_ID,
        range: `${quoteSheetName(found.sheetName)}!${found.cell}`,
        valueInputOption: 'USER_ENTERED',
        resource: { values: [[newText]] },
    });
    return { movie: found.movie, cell: found.cell, oldValue };
}

async function getMovies() {
    const auth = getGoogleAuth(['https://www.googleapis.com/auth/spreadsheets']);
    const sheets = google.sheets({ version: 'v4', auth });
    const sheetName = await getMoviesSheetName(sheets);
    const response = await sheets.spreadsheets.values.get({
        spreadsheetId: SPREADSHEET_ID,
        range: `${quoteSheetName(sheetName)}!A4:S500`,
    });
    const movies = [];
    for (const row of response.data.values || []) {
        if (row[0]) movies.push({
            number: row[0].toString().trim(),
            highlights: row[2]?.toString().trim() || 'لا توجد أحداث',
            repeats: row[8]?.toString().trim() || '0',
        });
        if (row[18]) movies.push({
            number: row[18].toString().trim(),
            highlights: row[16]?.toString().trim() || 'لا توجد أحداث',
            repeats: row[10]?.toString().trim() || '0',
        });
    }
    return movies.sort((a, b) => Number(a.number) - Number(b.number));
}

async function sendAllMovies(interaction) {
    const movies = await getMovies();
    if (movies.length === 0) {
        return interaction.editReply('❌ لا توجد أفلام في ورقة Movies.');
    }

    const lines = movies.map(movie =>
        `**فيلم ${movie.number}** — التكرار: **${movie.repeats}**\n🔍 ${movie.highlights}`
    );
    const chunks = [];
    let chunk = '';
    for (const line of lines) {
        const safeLine = line.length > 3900 ? `${line.slice(0, 3897)}...` : line;
        if ((chunk + '\n\n' + safeLine).length > 3900) {
            if (chunk) chunks.push(chunk);
            chunk = safeLine;
        } else {
            chunk += `${chunk ? '\n\n' : ''}${safeLine}`;
        }
    }
    if (chunk) chunks.push(chunk);

    await interaction.editReply({
        embeds: [new EmbedBuilder()
            .setTitle(`🎬 قائمة الأفلام (${movies.length})`)
            .setColor(0xEE1D23)
            .setDescription(chunks.shift())
            .setTimestamp()],
    });
    for (const nextChunk of chunks) {
        await interaction.followUp({
            embeds: [new EmbedBuilder().setColor(0xEE1D23).setDescription(nextChunk)],
        });
    }
}

// دالة كبيرة مسؤولة عن ترتيب الجدول بأكمله تصاعدياً وإضافة أو حذف حلقة منه
async function modifyAndSortEpisodes(options = {}) {
    const auth = getGoogleAuth(['https://www.googleapis.com/auth/spreadsheets']);
    const sheets = google.sheets({ version: 'v4', auth });

    // 1. جلب البيانات وهيكل الشيت
    const sheetMeta = await sheets.spreadsheets.get({ spreadsheetId: SPREADSHEET_ID });
    const sheet = sheetMeta.data.sheets.find(s => s.properties.title === SHEET_NAME);
    if (!sheet) throw new Error(`لم يتم العثور على ورقة العمل: ${SHEET_NAME}`);
    const sheetId = sheet.properties.sheetId;

    const currentSheetRowCount = sheet.properties.gridProperties.rowCount;

    const response = await sheets.spreadsheets.values.get({
        spreadsheetId: SPREADSHEET_ID,
        range: `${SHEET_NAME}!A4:S${currentSheetRowCount}`, 
    });

    const rows = response.data.values || [];
    let allEpisodes = [];

    // استخراج كافة الحلقات من الجانبين ووضعها في قائمة واحدة مؤقتة
    rows.forEach(row => {
        // الجانب الأيسر
        if (row[0] && row[0].toString().trim() !== '') {
            allEpisodes.push({
                number: row[0].toString().trim(),
                highlights: row[2] || '',
                rating: row[4] || '',
                notes: row[6] || '',
                watched: row[8] === 'TRUE' || row[8] === true
            });
        }
        // الجانب الأيمن
        if (row[18] && row[18].toString().trim() !== '') {
            allEpisodes.push({
                number: row[18].toString().trim(),
                highlights: row[16] || '',
                rating: row[14] || '',
                notes: row[12] || '',
                watched: row[10] === 'TRUE' || row[10] === true
            });
        }
    });

    // في حال كنت تضيف حلقة جديدة
    if (options.addEp) {
        if (allEpisodes.find(e => e.number === options.addEp)) {
            throw new Error(`الحلقة ${options.addEp} موجودة بالفعل!`);
        }
        allEpisodes.push({
            number: options.addEp,
            highlights: options.highlights || '',
            rating: '',
            notes: '',
            watched: false
        });
    }

    // في حال كنت تحذف حلقة
    if (options.removeEp) {
        const initialLength = allEpisodes.length;
        // الفلتر يستثني الحلقة المطابقة ويزيلها
        allEpisodes = allEpisodes.filter(e => e.number !== options.removeEp);
        if (allEpisodes.length === initialLength) {
            throw new Error(`الحلقة ${options.removeEp} غير موجودة.`);
        }
    }

    if (allEpisodes.length === 0) return;

    // 2. الترتيب الرقمي تصاعدياً
    allEpisodes.sort((a, b) => {
        const getNum = (str) => {
            const match = str.match(/\d+/);
            return match ? parseInt(match[0]) : 0;
        };
        return getNum(a.number) - getNum(b.number);
    });

    // 3. تحضير المصفوفة النهائية وقسمتها لنصفين لتعبئتها يميناً ويساراً
    const half = Math.ceil(allEpisodes.length / 2);
    const leftSide = allEpisodes.slice(0, half);
    const rightSide = allEpisodes.slice(half);

    const neededRows = Math.max(half, 20); // حد أدنى 20 صف للحفاظ على التنسيق الجمالي
    const writeRowsCount = Math.max(neededRows, rows.length); // تأكد من مسح الصفوف القديمة الفارغة

    const newGrid = [];

    // تعبئة البيانات في صفوف جديدة
    for (let i = 0; i < writeRowsCount; i++) {
        const row = Array(19).fill(''); // تكوين صف فارغ من 19 عمود
        const left = leftSide[i];
        const right = rightSide[i];

        if (left) {
            row[0] = left.number;
            row[2] = left.highlights;
            row[4] = left.rating;
            row[6] = left.notes;
            row[8] = left.watched;
        }
        if (right) {
            row[18] = right.number;
            row[16] = right.highlights;
            row[14] = right.rating;
            row[12] = right.notes;
            row[10] = right.watched;
        }
        newGrid.push(row);
    }

    const requests = [];

    // توسيع الجدول إذا لزم الأمر
    const requiredPhysicalRows = writeRowsCount + 3;
    if (requiredPhysicalRows > currentSheetRowCount) {
        requests.push({
            updateSheetProperties: {
                properties: {
                    sheetId: sheetId,
                    gridProperties: {
                        rowCount: requiredPhysicalRows,
                        columnCount: 19
                    }
                },
                fields: 'gridProperties(rowCount)'
            }
        });
    }

    // تحديث كل الخلايا بالأرقام المنسقة الجديدة
    requests.push({
        updateCells: {
            range: {
                sheetId: sheetId,
                startRowIndex: 3, 
                endRowIndex: writeRowsCount + 3,
                startColumnIndex: 0,
                endColumnIndex: 19
            },
            rows: newGrid.map(row => ({
                values: row.map(val => {
                    // الدالة التي تحفظ بيانات المربعات כقيمة بوليانية (صح وخطاء)
                    if (typeof val === 'boolean') return { userEnteredValue: { boolValue: val } };
                    return { userEnteredValue: { stringValue: String(val ?? '') } };
                })
            })),
            fields: 'userEnteredValue'
        }
    });

    // رسومات مربعات الصح للجانب الايسر
    requests.push({
        setDataValidation: {
            range: { sheetId: sheetId, startRowIndex: 3, endRowIndex: writeRowsCount + 3, startColumnIndex: 8, endColumnIndex: 9 },
            rule: { condition: { type: 'BOOLEAN' }, showCustomUi: true }
        }
    });

    // رسومات مربعات الصح للجانب الايمن
    requests.push({
        setDataValidation: {
            range: { sheetId: sheetId, startRowIndex: 3, endRowIndex: writeRowsCount + 3, startColumnIndex: 10, endColumnIndex: 11 },
            rule: { condition: { type: 'BOOLEAN' }, showCustomUi: true }
        }
    });

    // 4. تنفيذ التحديث الشامل دفعة واحدة
    await sheets.spreadsheets.batchUpdate({
        spreadsheetId: SPREADSHEET_ID,
        resource: { requests }
    });

    return allEpisodes.length;
}

// دالة لجلب قائمة الحلقات بالكامل لغرض عرضها ضمن أمر /all أو /unwatched
async function getEpisodes(onlyUnwatched = false) {
    const auth = getGoogleAuth(['https://www.googleapis.com/auth/spreadsheets']);
    const sheets = google.sheets({ version: 'v4', auth });

    const response = await sheets.spreadsheets.values.get({
        spreadsheetId: SPREADSHEET_ID,
        range: `${SHEET_NAME}!A4:S500`,
    });

    const rows = response.data.values || [];
    const eps = [];

    rows.forEach(row => {
        // التحقق من الجهة اليسرى
        if (row[0] && row[0].toString().trim() !== '') {
            const isChecked = row[8] === 'TRUE' || row[8] === true;
            if (!onlyUnwatched || !isChecked) {
                eps.push({
                    number: row[0].toString().trim(),
                    // إذا لم توجد أحداث مكتوبة ستظهر هذه الرسالة البديلة، يمكنك تعديلها
                    highlights: row[2] ? row[2].toString().trim() : 'لا توجد أحداث',
                    watched: isChecked
                });
            }
        }
        // التحقق من الجهة اليمنى
        if (row[18] && row[18].toString().trim() !== '') {
            const isChecked = row[10] === 'TRUE' || row[10] === true;
            if (!onlyUnwatched || !isChecked) {
                eps.push({
                    number: row[18].toString().trim(),
                     // الرسالة البديلة للجانب الأيمن أيضاً
                    highlights: row[16] ? row[16].toString().trim() : 'لا توجد أحداث',
                    watched: isChecked
                });
            }
        }
    });

    // ترتيب الحلقات رقمياً
    return eps.sort((a, b) => {
        const numA = parseInt(a.number.match(/\d+/)?.[0] || 0);
        const numB = parseInt(b.number.match(/\d+/)?.[0] || 0);
        return numA - numB;
    });
}

/**
 * دالة مسؤولة عن إرسال قائمة الحلقات بنظام الصفحات (Pagination)
 * @param {any} context - سياق التفاعل (Interaction or Message)
 */
async function sendPaginatedAll(context) {
    const isInteraction = !!context.isChatInputCommand;
    // جلب كافة الحلقات من الجدول
    const episodes = await getEpisodes(false);
    
    // إذا كان الجدول فارغاً
    if (episodes.length === 0) {
        const msg = '📭 لا توجد حلقات في الجدول حالياً.';
        return isInteraction ? context.editReply(msg) : context.reply(msg);
    }

    // إعدادات الصفحات
    const pageSize = 10; // عدد الحلقات في كل صفحة
    const totalPages = Math.ceil(episodes.length / pageSize);
    let currentPage = 1;

    // دالة لإنشاء الرسالة المزينة للصفحة الحالية
    const generateEmbed = (page) => {
        const start = (page - 1) * pageSize;
        const end = start + pageSize;
        const pageEpisodes = episodes.slice(start, end);
        
        // تنسيق قائمة الحلقات مع الرقم التسلسلي
        const listText = pageEpisodes.map((ep, index) => {
            const globalIndex = (page - 1) * pageSize + index + 1;
            return `**${globalIndex}.** ${ep.watched ? '✅' : '❌'} **حلقة ${ep.number}**: ${ep.highlights}`;
        }).join('\n');

        return new EmbedBuilder()
            .setTitle('📚 جميع حلقات كونان')
            .setColor(0x3498DB)
            .setDescription(`إجمالي الحلقات: **${episodes.length}**\n\n${listText}`)
            .setFooter({ text: `صفحة ${page} من ${totalPages}` })
            .setTimestamp();
    };

    // دالة لإنشاء أزرار التحكم في الصفحات
    const generateButtons = (page) => {
        return new ActionRowBuilder().addComponents(
            new ButtonBuilder()
                .setCustomId('first_page')
                .setLabel('First')
                .setStyle(ButtonStyle.Primary)
                .setDisabled(page === 1),
            new ButtonBuilder()
                .setCustomId('prev_page')
                .setLabel('Prev')
                .setStyle(ButtonStyle.Primary)
                .setDisabled(page === 1),
            new ButtonBuilder()
                .setCustomId('next_page')
                .setLabel('Next')
                .setStyle(ButtonStyle.Primary)
                .setDisabled(page === totalPages),
            new ButtonBuilder()
                .setCustomId('last_page')
                .setLabel('Last')
                .setStyle(ButtonStyle.Primary)
                .setDisabled(page === totalPages)
        );
    };

    const messageOptions = {
        embeds: [generateEmbed(currentPage)],
        components: [generateButtons(currentPage)]
    };

    let msg;
    try {
        // إرسال الرسالة الأولية
        if (isInteraction) {
            msg = await context.editReply(messageOptions);
        } else {
            msg = await context.reply(messageOptions);
        }
    } catch (err) {
        console.error('Error sending paginated message:', err);
        return;
    }

    // معرف المستخدم الذي طلب الأمر لضمان عدم تداخل التفاعلات
    const userId = isInteraction ? context.user.id : context.author.id;
    
    // إنشاء مجمع للتفاعل مع الأزرار لمدة دقيقتين
    const collector = msg.createMessageComponentCollector({
        filter: i => i.user.id === userId,
        time: 120000 // مدة صلاحية الأزرار (2 دقائق)
    });

    collector.on('collect', async i => {
        try {
            // تحديث الصفحة بناءً على الزر المضغوط
            if (i.customId === 'first_page') {
                currentPage = 1;
            } else if (i.customId === 'prev_page') {
                if (currentPage > 1) currentPage--;
            } else if (i.customId === 'next_page') {
                if (currentPage < totalPages) currentPage++;
            } else if (i.customId === 'last_page') {
                currentPage = totalPages;
            }

            // تحديث الرسالة بالبيانات الجديدة للأزرار والصفحة
            await i.update({
                embeds: [generateEmbed(currentPage)],
                components: [generateButtons(currentPage)]
            });
        } catch (err) {
            console.error('Error updating pagination:', err);
        }
    });

    // عند انتهاء مدة المجمع (التعطيل التلقائي للأزرار)
    collector.on('end', async () => {
        try {
            const disabledButtons = new ActionRowBuilder().addComponents(
                new ButtonBuilder().setCustomId('first_dis').setLabel('الأول').setStyle(ButtonStyle.Secondary).setDisabled(true),
                new ButtonBuilder().setCustomId('prev_dis').setLabel('السابق').setStyle(ButtonStyle.Secondary).setDisabled(true),
                new ButtonBuilder().setCustomId('next_dis').setLabel('التالي').setStyle(ButtonStyle.Secondary).setDisabled(true),
                new ButtonBuilder().setCustomId('last_dis').setLabel('الأخير').setStyle(ButtonStyle.Secondary).setDisabled(true)
            );
            await msg.edit({ components: [disabledButtons] });
        } catch (e) {
            // تجاهل الخطأ في حال كانت الرسالة قد حُذفت
        }
    });
}


// ==============================================================================
// 8. أحداث جاهزية البوت (Client Ready Events)
// ==============================================================================

// (Old listener functions removed - now handled by setupGeneralBot and setupConanBot)

// ==============================================================================
// 9. أوامر تفاعل المستخدم مع البوت العام (General Bot Slash Commands)
// ==============================================================================
// هذا الحدث يعمل كلما قام مستخدم باستخدام أمر بطريقة / داخل الديسكورد
function attachGeneralInteractionHandler() {
    generalClient.on('interactionCreate', async (interaction) => {
        // نتجاهل أي تفاعل ليس "أمر سلاش"
        if (!interaction.isChatInputCommand()) return;

    // ----- أمر عرض الردود التلقائية -----
    if (interaction.commandName === 'list_auto_replies') {
        console.log(`[User: ${interaction.user.tag}] executed /list_auto_replies`);
        if (autoRepliesMap.size === 0) {
            // ephemeral: true تعني أن الرسالة تظهر للمستخدم الذي كتب الأمر فقط (مخفية للآخرين)
            return interaction.reply({ content: 'لا توجد ردود تلقائية مضافة حالياً.', flags: MessageFlags.Ephemeral });
        }

        const embed = new EmbedBuilder()
            // غيّر العنوان هنا إذا أردت
            .setTitle('قائمة الردود التلقائية 🤖')
            // تستطيع اختيار لون محدد للرسالة، استبدل 0x00AE86 برمز الهيكس الذي يعجبك
            .setColor(0x00AE86)
            .setDescription('إليك الكلمات المفتاحية والردود الخاصة بها:')
            .setTimestamp();

        let list = '';
        autoRepliesMap.forEach((reply, trigger) => {
            // كيف يتم عرض كل اختصار، الشكل الحالي: **الكلمة** -> الرد
            list += `**${trigger}** -> ${reply}\n`;
        });

        // "الردود" هو العنوان الفرعي، يمكنك تغييره.
        embed.addFields({ name: 'الردود', value: list || 'فارغة' });

        await interaction.reply({ embeds: [embed] });
    }

    // ----- أمر إضافة رد تلقائي جديد -----
    if (interaction.commandName === 'add_reply') {
        const trigger = interaction.options.getString('trigger').trim();
        const reply = interaction.options.getString('reply').trim();
        console.log(`[User: ${interaction.user.tag}] executed /add_reply | Trigger: ${trigger} | Reply: ${reply}`);
        
        autoRepliesMap.set(trigger, reply);
        saveAutoReplies(); // حفظه في الملف للحفاظ عليه
        
        // غير نص رد البوت هنا عند النجاح (✅)
        await interaction.reply({ content: `✅ تم بنجاح إضافة/تحديث الرد التلقائي: **${trigger}** -> ${reply}` });
    }

    // ----- أمر إزالة رد تلقائي -----
    if (interaction.commandName === 'remove_reply') {
        const trigger = interaction.options.getString('trigger').trim();
        console.log(`[User: ${interaction.user.tag}] executed /remove_reply | Trigger: ${trigger}`);
        
        if (autoRepliesMap.has(trigger)) {
            autoRepliesMap.delete(trigger);
            saveAutoReplies();
            // نص الرد في حال تمكّن من الحذف
            await interaction.reply({ content: `✅ تم حذف الرد التلقائي الخاص بـ: **${trigger}**` });
        } else {
            // نص الرد في حال الكلمة غير موجودة أصلاً (الرسالة ستكون مخفية لك وحدك)
            await interaction.reply({ content: `❌ العبارة **${trigger}** غير موجودة في قائمة الردود.`, flags: MessageFlags.Ephemeral });
        }
    }

    // ----- أمر إرسال رسالة خاصة DM -----
    if (interaction.commandName === 'dm') {
        const targetUser = interaction.options.getUser('user');
        const content = interaction.options.getString('message');
        console.log(`[User: ${interaction.user.tag}] executed /dm | Target: ${targetUser.tag} | Message: ${content}`);
        
        try {
            await targetUser.send(content);
            // نص نجاح الإرسال الخاص
            await interaction.reply({ content: `✅ تم إرسال الرسالة بنجاح إلى **${targetUser.tag}**.` });
        } catch (err) {
            console.error(`Error sending DM to ${targetUser.id}:`, err);
            // نص فشل الإرسال (مثلاً لو أن المستخدم قفل خاصية استقبال الرسائل الخاصة)
            await interaction.reply({ content: `❌ فشل إرسال الرسالة. قد تكون رسائل المستخدم الخاصة مغلقة (DMs closed).`, flags: MessageFlags.Ephemeral });
        }
    }

    // ----- أمر إرسال رسالة عامة بالبوت -----
    if (interaction.commandName === 'send') {
        const content = interaction.options.getString('message');
        console.log(`[User: ${interaction.user.tag}] executed /send | Message: ${content}`);
        
        try {
            await interaction.channel.send(content);
            // ephemeral يضمن أنك الوحيـد الذي يرى رسالة تأكيد الإرسال هذه
            await interaction.reply({ content: `✅ تم إرسال الرسالة في القناة.`, flags: MessageFlags.Ephemeral });
        } catch (err) {
            console.error(`Error sending message in channel:`, err);
            await interaction.reply({ content: `❌ فشل إرسال الرسالة. هل يمتلك البوت صلاحية الكتابة في هذه القناة؟`, flags: MessageFlags.Ephemeral });
        }
    }

    // ----- أمر حذف الرسائل clear -----
    if (interaction.commandName === 'clear') {
        const amount = interaction.options.getInteger('number');
        console.log(`[User: ${interaction.user.tag}] executed /clear | Amount: ${amount}`);

        try {
            // تنفيذ الحذف
            const deleted = await interaction.channel.bulkDelete(amount, true);
            await interaction.reply({ content: `✅ تم حذف **${deleted.size}** رسالة بنجاح.`, flags: MessageFlags.Ephemeral });
        } catch (err) {
            console.error('Error in /clear command:', err);
            await interaction.reply({ content: `❌ حدث خطأ أثناء محاولة حذف الرسائل: ${err.message}`, flags: MessageFlags.Ephemeral });
        }
    }

    // ----- أمر إعطاء رتبة role -----
    if (interaction.commandName === 'role') {
        const targetMember = interaction.options.getMember('user');
        const targetRole = interaction.options.getRole('role');
        const targetUserOption = interaction.options.getUser('user');
        console.log(`[User: ${interaction.user.tag}] executed /role | Target: ${targetUserOption?.tag} | Role: ${targetRole?.name}`);

        try {
            if (!targetMember) {
                return await interaction.reply({ content: `❌ العضو غير موجود في هذا السيرفر.`, flags: MessageFlags.Ephemeral });
            }

            const botMember = interaction.guild.members.me;
            if (botMember.roles.highest.position <= targetRole.position) {
                return await interaction.reply({ content: `❌ رتبة البوت أدنى أو تساوي الرتبة التي تحاول إعطاءها لذلك لا يملك صلاحية لذلك.`, flags: MessageFlags.Ephemeral });
            }

            await targetMember.roles.add(targetRole);
            await interaction.reply({ content: `✅ تم إعطاء رتبة **${targetRole.name}** للعضو **<@${targetMember.id}>** بنجاح.` });
        } catch (err) {
            console.error('Error in /role command:', err);
            await interaction.reply({ content: `❌ حدث خطأ أثناء محاولة إعطاء الرتبة: ${err.message}`, flags: MessageFlags.Ephemeral });
        }
    }

    // ----- أوامر eWeLink (Sonoff) -----
    if (interaction.commandName === 'devices') {
        if (ewelinkDevices.length === 0) {
            return interaction.reply({ content: '❌ لم يتم العثور على أجهزة متصلة أو أن البوت لم يكتمل اتصاله بعد.', flags: MessageFlags.Ephemeral });
        }

        const embed = new EmbedBuilder()
            .setTitle('🏠 قائمة أجهزة eWeLink')
            .setColor(0x3498DB)
            .setTimestamp();

        let deviceList = '';
        ewelinkDevices.forEach((device, index) => {
            const status = device.itemData.params.switch === 'on' ? '🟢 ON' : '🔴 OFF';
            deviceList += `${index + 1}. **${device.itemData.name}** - الحالة: ${status}\n`;
        });

        embed.setDescription(deviceList || 'لا توجد أجهزة متوفرة.');
        return interaction.reply({ embeds: [embed] });
    }

    if (['on_all', 'off_all', 'turn'].includes(interaction.commandName)) {
        if (!ewelinkConnection || !ewelinkConnection.at) {
            return interaction.reply({ content: '❌ البوت غير متصل بـ eWeLink حالياً.', flags: MessageFlags.Ephemeral });
        }
        if (ewelinkDevices.length === 0) {
            return interaction.reply({ content: '❌ لا توجد أجهزة للتحكم بها.', flags: MessageFlags.Ephemeral });
        }

        const action = interaction.commandName;
        await interaction.deferReply();
        
        try {
            let successCount = 0;
            for (const device of ewelinkDevices) {
                let targetState = '';
                if (action === 'on_all') targetState = 'on';
                else if (action === 'off_all') targetState = 'off';
                else if (action === 'turn') {
                    targetState = device.itemData.params.switch === 'on' ? 'off' : 'on';
                }

                const res = await manualSetDeviceStatus(
                    ewelinkConnection.region,
                    ewelinkConnection.at,
                    device.itemData.deviceid,
                    targetState
                );

                if (res && (res.error === 0 || !res.error)) {
                    device.itemData.params.switch = targetState;
                    successCount++;
                }
            }

            const statusMsg = action === 'turn' ? 'تم تبديل حالة' : (action === 'on_all' ? 'تم تشغيل' : 'تم إطفاء');
            await interaction.editReply(`✅ ${statusMsg} **${successCount}** جهاز بنجاح.`);
        } catch (err) {
            console.error(`Error in ${action}:`, err);
            await interaction.editReply(`❌ حدث خطأ أثناء تنفيذ الأمر: ${err.message}`);
        }
    }
    });
}

// ==============================================================================
// 10. أوامر تفاعل المستخدم مع بوت كونان (Conan Bot Slash Commands)
// ==============================================================================
function attachConanInteractionHandler() {
    conanClient.on('interactionCreate', async (interaction) => {
        if (!interaction.isChatInputCommand() && !interaction.isButton()) return;

    // ----- أمر إضافة حلقة جديدة بشكل يدوي للجدول -----
    if (interaction.commandName === 'add_episode') {
        // deferReply تعطي البوت وقتًا أطول للرد (لأن الاتصال بجوجل شيت قد يأخذ ثوانٍ طويلة)
        await interaction.deferReply();
        const episodeNum = interaction.options.getString('episode');
        const highlights = interaction.options.getString('highlights') || '';
        console.log(`[User: ${interaction.user.tag}] executed /add_episode | Episode: ${episodeNum} | Highlights: ${highlights}`);

        try {
            await modifyAndSortEpisodes({ addEp: episodeNum, highlights: highlights });
            // نص الرد الناجح
            await interaction.editReply(`✅ تمت إضافة الحلقة **${episodeNum}** بنجاح في مكانها الصحيح وترتيب الجدول.`);
        } catch (err) {
            // نص الرد في حال الخطأ
            await interaction.editReply(`❌ فشل إضافة الحلقة: ${err.message}`);
        }
    }

    // ----- أمر إزالة حلقة من الجدول -----
    if (interaction.commandName === 'remove_episode') {
        await interaction.deferReply();
        const episodeNum = interaction.options.getString('episode');
        console.log(`[User: ${interaction.user.tag}] executed /remove_episode | Episode: ${episodeNum}`);

        try {
            await modifyAndSortEpisodes({ removeEp: episodeNum });
            // الرد الناجح
            await interaction.editReply(`✅ تم حذف الحلقة **${episodeNum}** وإعادة ضبط ترتيب الجدول.`);
        } catch (err) {
            // في حال الفشل
            await interaction.editReply(`❌ فشل حذف الحلقة: ${err.message}`);
        }
    }

    // ----- أمر إظهار الحلقات الغير مشاهدة -----
    if (interaction.commandName === 'unwatched') {
        console.log(`[User: ${interaction.user.tag}] executed /unwatched`);
        try {
            await interaction.deferReply();
            const unwatched = await getEpisodes(true); // جلب الحلقات الغير المشاهدة فقط (true)

            if (unwatched.length === 0) {
                // يمكنك تغيير هذه الجملة التشجيعية إذا أنهيت جميع المسلسل!
                return interaction.editReply('🎉 جميع الحلقات تمت مشاهدتها! لا توجد حلقات غير مشاهدة حالياً.');
            }

            // كيف ترغب في تنسيق عرض اسم الحلقة وأحداثها للمستخدم:
            const formatList = (list) => list.map(ep => `• **حلقة ${ep.number}**: ${ep.highlights}`).join('\n');

            const embed = new EmbedBuilder()
                // عنوان رسالة الغير مشاهد
                .setTitle('📺 الحلقات غير المشاهدة (مع أبرز الأحداث)')
                // اللون المستخدم لرسالة غير المشاهد (اختياري، مثلاً برتقالي = 0xFFA500)
                .setColor(0xFFA500)
                .setDescription(`يوجد حالياً **${unwatched.length}** حلقة غير مشاهدة:\n\n${formatList(unwatched)}`)
                .setTimestamp()
                // رسالة تذييل الرسالة أسفلاً
                .setFooter({ text: 'استخدم رقم الحلقة لتحديث حالتها' });

            // نظام أمان الديسكورد يمنع إرسال أكثر من 4000 حرف في رسالة واحدة
            // هذا الكود يقسم القائمة إذا كانت طويلة ويطبعها في عدة رسائل
            if (embed.data.description.length > 4000) {
                const intro = `يوجد حالياً **${unwatched.length}** حلقة غير مشاهدة:\n\n`;
                let chunks = [];
                let currentChunk = intro;

                unwatched.forEach(ep => {
                    const line = `• **حلقة ${ep.number}**: ${ep.highlights}\n`;
                    if ((currentChunk + line).length > 4000) {
                        chunks.push(currentChunk);
                        currentChunk = line;
                    } else {
                        currentChunk += line;
                    }
                });
                if (currentChunk) chunks.push(currentChunk);

                // النص التوضيحي الذي يظهر قبل إرسال الرسالة المقسمة
                await interaction.editReply({ content: 'القائمة طويلة، إليك الحلقات:' });
                for (let chunk of chunks) {
                    const chunkEmbed = new EmbedBuilder()
                        // نفس اللون، غيره إذا شئت (مثلاً 0xFFFFFF للون الأبيض)
                        .setColor(0xFFA500)
                        .setDescription(chunk);
                    await interaction.followUp({ embeds: [chunkEmbed] });
                }
            } else {
                await interaction.editReply({ embeds: [embed] });
            }

        } catch (err) {
            console.error('Error in unwatched command:', err);
            await interaction.editReply(`❌ حدث خطأ أثناء جلب البيانات: ${err.message}`);
        }
    }

    // ----- أمر إظهار كافة الحلقات -----
    if (interaction.commandName === 'all') {
        console.log(`[User: ${interaction.user.tag}] executed /all`);
        try {
            await interaction.deferReply();
            await sendPaginatedAll(interaction);
        } catch (err) {
            console.error('Error in all command:', err);
            if (interaction.deferred) {
                await interaction.editReply(`❌ حدث خطأ أثناء جلب البيانات: ${err.message}`);
            } else {
                await interaction.reply(`❌ حدث خطأ أثناء جلب البيانات: ${err.message}`);
            }
        }
    }

    // ----- أمر إظهار جميع الأفلام -----
    if (interaction.commandName === 'all_m') {
        console.log(`[User: ${interaction.user.tag}] executed /all_m`);
        try {
            await interaction.deferReply();
            await sendAllMovies(interaction);
        } catch (err) {
            console.error('Error in all_m command:', err);
            const errorMessage = `❌ حدث خطأ أثناء جلب الأفلام: ${err.message}`;
            if (interaction.deferred) {
                await interaction.editReply(errorMessage);
            } else {
                await interaction.reply(errorMessage);
            }
        }
    }

    // ----- أمر الرابط لصفحة ويكي الخاصة بحلقة محددة -----
    if (interaction.commandName === 'wiki') {
        const episode = interaction.options.getString('episode').trim();
        console.log(`[User: ${interaction.user.tag}] executed /wiki | Episode: ${episode}`);
        // الرابط الثابت للموسوعة، البوت يقوم بإبدال المسافات التلقائية ليقوم بتشكيل الرابط الصحيح
        const wikiUrl = `https://www.detectiveconanworld.com/wiki/Episode_${episode.replace(/\s+/g, '_')}`;

        const embed = new EmbedBuilder()
            // عنوان رسالة رابط الويكي
            .setTitle(`🔍 Detective Conan World Wiki: Episode ${episode}`)
            // اللون (مثلاً الأحمر القاني لكونان = 0xEE1D23)
            .setColor(0xEE1D23)
            // نص الدعوة وأسفله رابط مخفي في الكلمة. لتغيير الكلمة غير [اضغط هنا لفتح الصفحة]
            .setDescription(`إليك رابط صفحة الويكي الخاصة بالحلقة رقم **${episode}**:\n\n🔗 [اضغط هنا لفتح الصفحة](${wikiUrl})`)
            // الصورة المصغرة في الرسالة (أيقونة اللوقو الخاص بالموقع)
            .setThumbnail('https://www.detectiveconanworld.com/wiki/images/thumb/e/e3/Detective_Conan_Logo.png/300px-Detective_Conan_Logo.png')
            .setTimestamp()
            // يمكنك تغيير التذييل بأسفل الرد
            .setFooter({ text: 'Detective Conan World' });

        await interaction.reply({ embeds: [embed] });
    }

    // ----- أمر إظهار المعلومات وقائمة الأوامر -----
    if (interaction.commandName === 'commands') {
        console.log(`[User: ${interaction.user.tag}] executed /commands`);
        const embed = new EmbedBuilder()
            // عنوان أداة المساعدة (الدليل)
            .setTitle('📖 دليل أوامر ومميزات البوت')
            // اللون العام للأداة
            .setColor(0xEE1D23)
            // استخدام صورة الأفاتار أو صورة حساب البوت نفسة كصورة مصغرة
            .setThumbnail(conanClient.user.displayAvatarURL())
            .setDescription('إليك قائمة بجميع الأوامر المتاحة مقسمة حسب الفئة:')
            // إضافة حقول (Fields) مختلفة. كل حقل هو كتلة نصية لها عنوان وقيمة.
            .addFields(
                { 
                    name: '🕵️‍♂️ أوامر كونان والجدول (Conan & Sheets)', 
                    value: 
                    // إذا قمت بإضافة أي أمر جديد لاحقاً، تأكد من كتابة سطر هنا لشرح الأمر للمستخدمين
                    '• `/all` أو `!all`: عرض جميع الحلقات المتوفرة.\n' +
                    '• `/unwatched` أو `!unwatched`: عرض الحلقات التي لم تشاهد.\n' +
                    '• `/episode_info` أو `!ep_info`: عرض أحداث وتقييم حلقة.\n' +
                    '• `/add_highlights` أو `!add_hdth`: تحديث أبرز أحداث حلقة.\n' +
                    '• `/sort_episodes` أو `!sort`: ترتيب الجدول تصاعدياً.\n' +
                    '• `/wiki` أو `!wiki`: رابط الويكي لأي حلقة.\n' +
                    '• `/add_episode` أو `!add`: إضافة حلقة جديدة.\n' +
                    '• `/remove_episode` أو `!remove`: حذف حلقة من الجدول.\n\n' +
                    '**أوامر الأفلام:**\n' +
                    '• `/all_m`: عرض كل الأفلام مع أحداثها وتكرارها.\n' +
                    '• `/movie_highlights` أو `!movie_hdth رقم | نص`: تحديث أبرز أحداث فيلم.'
                },
                { 
                    name: '✨ الميزة الخاصة (Auto-Log)', 
                    value: 
                    // سيتولى البوت منشنة القناة الخاصة بالمراقبة لتذكير المستخدم بمكان إرسال الأرقام
                    `أرسل **رقم الحلقة** في <#${TARGET_CHANNEL_ID}> لتغيير حالة مشاهدتها.\n` +
                    `أرسل **رقم الفيلم** في <#${MOVIES_CHANNEL_ID}> لزيادة عدد مرات تكراره تلقائياً.`
                }
            )
            // التذييل النهائي للرسالة
            .setFooter({ text: 'Detective Conan Bot • مساعدك الشخصي' })
            .setTimestamp();

        await interaction.reply({ embeds: [embed] });
    }

    // ----- أمر جلب التفاصيل الخاصة بحلقة واحدة -----
    if (interaction.commandName === 'episode_info') {
        const episodeNum = interaction.options.getString('episode').trim();
        console.log(`[User: ${interaction.user.tag}] executed /episode_info | Episode: ${episodeNum}`);
        try {
            await interaction.deferReply();
            
            const auth = getGoogleAuth(['https://www.googleapis.com/auth/spreadsheets']);
            const sheets = google.sheets({ version: 'v4', auth });

            const response = await sheets.spreadsheets.values.get({
                spreadsheetId: SPREADSHEET_ID,
                range: `${SHEET_NAME}!A4:S500`, // نطاق القراءة
            });

            const rows = response.data.values || [];
            let found = null; // سيخزن بيانات الحلقة إذا وجدها المكرر

            for (let row of rows) {
                // البحث في الجانب الأيسر (العمودي A)
                if (row[0] && row[0].toString().trim() === episodeNum) {
                    found = {
                        number: row[0].toString().trim(),
                        // إذا لم يجد قيم هنا، يمكنك تغيير القيمة الافتراضية البديلة (مثلاً من "لا توجد أحداث" لغيرها)
                        highlights: row[2] ? row[2].toString().trim() : 'لا توجد أحداث مرتبطة',
                        rating: row[4] ? row[4].toString().trim() : 'لم يتم التقييم بعد',
                        notes: row[6] ? row[6].toString().trim() : 'لا توجد ملاحظات',
                        watched: row[8] === 'TRUE' || row[8] === true
                    };
                    break;
                }
                // البحث في الجانب الأيمن (العمودي S)
                if (row[18] && row[18].toString().trim() === episodeNum) {
                    found = {
                        number: row[18].toString().trim(),
                        highlights: row[16] ? row[16].toString().trim() : 'لا توجد أحداث مرتبطة',
                        rating: row[14] ? row[14].toString().trim() : 'لم يتم التقييم بعد',
                        notes: row[12] ? row[12].toString().trim() : 'لا توجد ملاحظات',
                        watched: row[10] === 'TRUE' || row[10] === true
                    };
                    break;
                }
            }

            if (!found) { // تعني أنه لم يجد أي حلقة بهذا الإسم
                return interaction.editReply(`❌ لم يتم العثور على الحلقة رقم **${episodeNum}** في الجدول.`);
            }

            const embed = new EmbedBuilder()
                // عنوان رسالة التفاصيل
                .setTitle(`🎬 معلومات الحلقة: ${found.number}`)
                // تخصيص اللون ليكون أخضر للحلقة المشاهدة (0x2ECC71) وأحمر لغير المشاهدة (0xE74C3C)
                .setColor(found.watched ? 0x2ECC71 : 0xE74C3C)
                // بناء هيكل المعلومات داخل الرسالة
                .addFields(
                    { name: '⭐ التقييم', value: found.rating, inline: true }, // inline: true تعني أن الخانة يمكن أن تظهر بجانب خانة أخرى وليس بشرط أسفلها
                    { name: '📺 الحالة', value: found.watched ? '✅ تمت المشاهدة' : '❌ غير مشاهدة', inline: true },
                    { name: '🔍 أبرز الأحداث', value: found.highlights },
                    { name: '📝 الملاحظات', value: found.notes }
                )
                .setTimestamp()
                .setFooter({ text: 'Detective Conan Bot' });

            await interaction.editReply({ embeds: [embed] });

        } catch (err) {
            console.error('Error in episode_info command:', err);
            // نص في حال فشل الكود لجلب المعلومات (مثلاً لا يوجد اتصال قوي أو API به خطأ)
            await interaction.editReply(`❌ حدث خطأ أثناء جلب المعلومات: ${err.message}`);
        }
    }

    // ----- أمر تحديث وإضافة أحداث لحلقة بالجدول (عبر البوت السريع) -----
    if (interaction.commandName === 'add_highlights') {
        const episodeNum = interaction.options.getString('episode').trim();
        const newText = interaction.options.getString('text').trim();
        console.log(`[User: ${interaction.user.tag}] executed /add_highlights | Episode: ${episodeNum} | Text: ${newText}`);
        
        try {
            await interaction.deferReply();
            const auth = getGoogleAuth(['https://www.googleapis.com/auth/spreadsheets']);
            const sheets = google.sheets({ version: 'v4', auth });

            // 1. جلب البيانات للبحث عن الحلقة لتحديد صفها ورقم الخلية
            const response = await sheets.spreadsheets.values.get({
                spreadsheetId: SPREADSHEET_ID,
                range: `${SHEET_NAME}!A4:S500`,
            });

            const rows = response.data.values || [];
            let targetCell = null;
            let oldValue = ''; // لتخزين الحدث القديم قبل طمسه

            for (let i = 0; i < rows.length; i++) {
                const row = rows[i];
                const actualRow = i + 4;
                
                // الجانب الأيسر (A)
                if (row[0] && row[0].toString().trim() === episodeNum) {
                    targetCell = `C${actualRow}`; // العمود الخاص بالأحداث على الجانب الأيسر
                    oldValue = row[2] || 'لا يوجد نص سابق';
                    break;
                }
                // الجانب الأيمن (S)
                if (row[18] && row[18].toString().trim() === episodeNum) {
                    targetCell = `Q${actualRow}`; // العمود الخاص بالأحداث على الجانب الأيمن
                    oldValue = row[16] || 'لا يوجد نص سابق';
                    break;
                }
            }

            if (!targetCell) { // لم يتم إيجاد الخلية أو الحلقة للقيام باستبدالها
                return interaction.editReply(`❌ لم يتم العثور على الحلقة رقم **${episodeNum}** في الجدول.`);
            }

            // 2. تحديث الخلية بالقيم الجديدة (نص الحدث الجديد)
            await sheets.spreadsheets.values.update({
                spreadsheetId: SPREADSHEET_ID,
                range: `${SHEET_NAME}!${targetCell}`,
                valueInputOption: 'USER_ENTERED', // لضمان تنسيقه كإدخال بشري سليم وليس كمعادلات خطيرة
                resource: { values: [[newText]] },
            });

            const embed = new EmbedBuilder()
                // رسالة نجاح تحديث الخلية
                .setTitle(`✅ تم تحديث أبرز أحداث الحلقة ${episodeNum}`)
                // يمكنك تغيير لون رسالة التحديث. الأخضر الفاتح هو (0x2ECC71)
                .setColor(0x2ECC71)
                // عرض الخلية واسمها للطمأنينة
                .setDescription(`تم تحديث الخانة **${targetCell}** بنجاح.`)
                // طباعة الفارق (الحدث القديم أمامه الجديد) للمقارنة
                .addFields(
                    { name: '⬅️ النص السابق', value: oldValue },
                    { name: '➡️ النص الجديد', value: newText }
                )
                .setTimestamp();

            await interaction.editReply({ embeds: [embed] });

        } catch (err) {
            console.error('Error in add_highlights command:', err);
            await interaction.editReply(`❌ حدث خطأ أثناء التحديث: ${err.message}`);
        }
    }

    // ----- أمر تحديث أبرز أحداث فيلم -----
    if (interaction.commandName === 'movie_highlights') {
        const movieNumber = interaction.options.getString('movie').trim();
        const newText = interaction.options.getString('text').trim();
        try {
            await interaction.deferReply();
            const result = await updateMovieHighlights(movieNumber, newText);
            const embed = new EmbedBuilder()
                .setTitle(`✅ تم تحديث أبرز أحداث الفيلم ${result.movie}`)
                .setColor(0x2ECC71)
                .setDescription(`تم تحديث الخانة **${result.cell}** في ورقة **${MOVIES_SHEET_NAME}**.`)
                .addFields(
                    { name: '⬅️ النص السابق', value: result.oldValue.toString().slice(0, 1024) },
                    { name: '➡️ النص الجديد', value: newText.slice(0, 1024) }
                )
                .setTimestamp();
            await interaction.editReply({ embeds: [embed] });
        } catch (err) {
            console.error('Error in movie_highlights command:', err);
            await interaction.editReply(`❌ حدث خطأ أثناء تحديث أحداث الفيلم: ${err.message}`);
        }
    }

    // ----- أمر ترتيب كافة الحلقات في الجدول -----
    if (interaction.commandName === 'sort_episodes') {
        console.log(`[User: ${interaction.user.tag}] executed /sort_episodes`);
        await interaction.deferReply();
        try {
            await modifyAndSortEpisodes();
            // رسالة النجاح الخاصة بأمر الترتيب
            await interaction.editReply('✅ تم إعادة ترتيب الجدول وتحديث كافة البيانات بنجاح.');
        } catch (err) {
            console.error('Error in sort_episodes command:', err);
            // رسالة إخفاق الترتيب
            await interaction.editReply(`❌ حدث خطأ أثناء الترتيب: ${err.message}`);
        }
    }
    
    // ----- أمر إرسال ملف الكود المصدري للبوت -----
    if (interaction.commandName === 'the_code') {
        process.stdout.write(`[User: ${interaction.user.tag}] executed /the_code\n`);
        try {
            await interaction.reply({
                content: 'إليك ملف الكود المصدري كاملاً (index.js):',
                files: [path.join(__dirname, 'index.js')],
                flags: MessageFlags.None // الرد ليس مخفي للمستخدم فقط 
            });
        } catch (err) {
            console.error('Error in the_code command:', err);
            await interaction.reply({ content: `❌ حدث خطأ أثناء إرسال الكود: ${err.message}`, flags: MessageFlags.None });
        }
    }
    });
}

// ==============================================================================
// 11. تعامل البوت مع الرسائل العادية (غير السلاش) والمستقبلات
// ==============================================================================

// --- نظام الردود التلقائية لـ General Bot ---
function attachGeneralMessageHandler() {
    generalClient.on('messageCreate', async (message) => {
    // تجاهل الرد إذا كان صاحب الرسالة هو في الأساس بوت، لتجنب حدوث كوارث (حلقات تكرار لانهائية)
    if (message.author.bot) return;

    const content = message.content.trim(); // الرسالة نظيفة وبدون مسافات إضافية يمينًا أو يساراً
    const prefix = config.prefix || '!'; // بادئة البوت العادي في حال أردت استخدام أوامره بدون السلاش. يمكنك تغييره من config.json

    // 1. التحقق الاستباقي من الرد للكلمات المحفوظة: الردود التي ليس بها بريفكس (لا تسبقها علامة تعجب)
    if (autoRepliesMap.has(content)) {
        console.log(`[Auto-Reply] User: ${message.author.tag} | Trigger: ${content}`);
        return message.reply(autoRepliesMap.get(content));
    }

    // 2. التحقق من الرد مع بريفكس (مثل !اكتب)
    if (content.startsWith(prefix)) {
        const fullContent = content.slice(prefix.length).trim();
        const args = fullContent.split(/\s+/);
        if (args.length === 0) return;
        
        const command = args[0].toLowerCase();
        const restOfContent = fullContent.slice(args[0].length).trim();
        const isAdmin = message.member.permissions.has(PermissionFlagsBits.Administrator);

        // ----- أمر عرض الردود التلقائية -----
        if (command === 'list') {
            if (autoRepliesMap.size === 0) {
                return message.reply('لا توجد ردود تلقائية مضافة حالياً.').then(msg => setTimeout(() => { msg.delete().catch(() => {}); message.delete().catch(() => {}); }, 5000));
            }

            const embed = new EmbedBuilder()
                .setTitle('قائمة الردود التلقائية 🤖')
                .setColor(0x00AE86)
                .setDescription('إليك الكلمات المفتاحية والردود الخاصة بها:')
                .setTimestamp();

            let list = '';
            autoRepliesMap.forEach((reply, trigger) => {
                list += `**${trigger}** -> ${reply}\n`;
            });

            embed.addFields({ name: 'الردود', value: list || 'فارغة' });
            return message.reply({ embeds: [embed] });
        }

        // ----- أمر إضافة رد تلقائي جديد -----
        if (command === 'add_r' && isAdmin) {
            const parts = restOfContent.split('|').map(p => p.trim());
            if (parts.length < 2) {
                return message.reply('❌ الاستخدام الصحيح: `!add_r الكلمة | الرد`').then(msg => setTimeout(() => { msg.delete().catch(() => {}); message.delete().catch(() => {}); }, 5000));
            }
            const [trigger, reply] = parts;
            autoRepliesMap.set(trigger, reply);
            saveAutoReplies();
            message.delete().catch(() => {});
            return message.channel.send(`✅ تم إضافة الرد: **${trigger}** -> ${reply}`);
        }

        // ----- أمر إزالة رد تلقائي -----
        if (command === 'remove_r' && isAdmin) {
            const trigger = restOfContent;
            if (!trigger) return message.reply('❌ يرجى كتابة الكلمة المراد حذفها.');
            if (autoRepliesMap.has(trigger)) {
                autoRepliesMap.delete(trigger);
                saveAutoReplies();
                message.delete().catch(() => {});
                return message.channel.send(`✅ تم حذف الرد التلقائي الخاص بـ: **${trigger}**`);
            } else {
                return message.reply(`❌ العبارة **${trigger}** غير موجودة.`);
            }
        }

        // ----- أمر إرسال رسالة خاصة DM -----
        if (command === 'dm' && isAdmin) {
            const targetUser = message.mentions.users.first();
            const dmParts = restOfContent.split(/\s+/);
            const userArg = dmParts[0];
            const msgContent = restOfContent.slice(userArg.length).trim();

            if (!targetUser || !msgContent) {
                return message.reply('❌ الاستخدام الصحيح: `!dm @المستخدم الرسالة`');
            }

            try {
                await targetUser.send(msgContent);
                message.delete().catch(() => {});
                return message.channel.send(`✅ تم إرسال الرسالة بنجاح إلى **${targetUser.tag}**.`);
            } catch (err) {
                return message.reply(`❌ فشل إرسال الرسالة. قد تكون رسائل المستخدم مغلقة.`);
            }
        }

        // ----- أمر إرسال رسالة عامة بالبوت -----
        if (command === 'send' && isAdmin) {
            if (!restOfContent) return message.reply('❌ يرجى كتابة الرسالة المراد إرسالها.');
            await message.channel.send(restOfContent);
            return message.delete().catch(() => {});
        }

        // ----- أمر حذف الرسائل clear -----
        if (command === 'clear') {
            if (!isAdmin) return;
            const amount = parseInt(args[1]);
            if (!isNaN(amount) && amount >= 1 && amount <= 100) {
                try {
                    await message.channel.bulkDelete(amount + 1, true);
                    console.log(`[User: ${message.author.tag}] executed !clear | Amount: ${amount}`);
                } catch (err) {
                    console.error('Error in prefix clear command:', err);
                }
                return;
            }
        }

        // Fallback to auto-replies with prefix
        if (autoRepliesMap.has(fullContent)) {
            console.log(`[Auto-Reply-Prefix] User: ${message.author.tag} | Trigger: ${fullContent}`);
            return message.reply(autoRepliesMap.get(fullContent));
        }
    }
    });
}

// --- تحديث جدول كونان لـ Conan Bot ---
function attachConanMessageHandler() {
    conanClient.on('messageCreate', async (message) => {
    if (message.author.bot) return;

    const content = message.content.trim();
    const prefix = config.prefix || '!';

    // 1. التعامل مع الأوامر التي تبدأ بـ !
    if (content.startsWith(prefix)) {
        const fullContent = content.slice(prefix.length).trim();
        const args = fullContent.split(/\s+/);
        if (args.length === 0) return;

        const command = args[0].toLowerCase();
        const restOfContent = fullContent.slice(args[0].length).trim();

        // ----- أمر إضافة حلقة !add -----
        if (command === 'add') {
            const ep = args[1];
            if (!ep) return message.reply('❌ يرجى كتابة رقم الحلقة: `!add 123`');
            try {
                const waitMsg = await message.reply('⏳ جاري إضافة الحلقة...');
                await modifyAndSortEpisodes({ addEp: ep });
                await waitMsg.edit(`✅ تمت إضافة الحلقة **${ep}** بنجاح.`);
                return message.delete().catch(() => {});
            } catch (err) {
                return message.reply(`❌ فشل: ${err.message}`);
            }
        }

        // ----- أمر حذف حلقة !remove -----
        if (command === 'remove') {
            const ep = args[1];
            if (!ep) return message.reply('❌ يرجى كتابة رقم الحلقة: `!remove 123`');
            try {
                const waitMsg = await message.reply('⏳ جاري حذف الحلقة...');
                await modifyAndSortEpisodes({ removeEp: ep });
                await waitMsg.edit(`✅ تم حذف الحلقة **${ep}** بنجاح.`);
                return message.delete().catch(() => {});
            } catch (err) {
                return message.reply(`❌ فشل: ${err.message}`);
            }
        }

        // ----- أمر الحلقات غير المشاهدة !unwatched -----
        if (command === 'unwatched') {
            try {
                const unwatched = await getEpisodes(true);
                if (unwatched.length === 0) return message.reply('🎉 جميع الحلقات تمت مشاهدتها!');

                const formatList = (list) => list.map(ep => `• **حلقة ${ep.number}**: ${ep.highlights}`).join('\n');
                const embed = new EmbedBuilder().setTitle('📺 الحلقات غير المشاهدة').setColor(0xFFA500).setDescription(formatList(unwatched).slice(0, 4000)).setTimestamp();
                return message.reply({ embeds: [embed] });
            } catch (err) {
                return message.reply(`❌ خطأ: ${err.message}`);
            }
        }

        // ----- أمر جميع الحلقات !all -----
        if (command === 'all') {
            try {
                await sendPaginatedAll(message);
            } catch (err) {
                console.error('Error in !all command:', err);
                return message.reply(`❌ خطأ: ${err.message}`);
            }
        }


        // ----- أمر الويكي !wiki -----
        if (command === 'wiki') {
            const ep = args[1];
            if (!ep) return message.reply('❌ يرجى كتابة رقم الحلقة: `!wiki 123`');
            const wikiUrl = `https://www.detectiveconanworld.com/wiki/Episode_${ep.replace(/\s+/g, '_')}`;
            const embed = new EmbedBuilder().setTitle(`🔍 Wiki: Episode ${ep}`).setColor(0xEE1D23).setDescription(`🔗 [اضغط هنا للفتح](${wikiUrl})`).setTimestamp();
            return message.reply({ embeds: [embed] });
        }

        // ----- أمر معلومات حلقة !ep_info -----
        if (command === 'ep_info') {
            const ep = args[1];
            if (!ep) return message.reply('❌ يرجى كتابة رقم الحلقة: `!ep_info 123`');
            try {
                const auth = new google.auth.GoogleAuth({ keyFile: path.join(__dirname, 'credentials.json'), scopes: ['https://www.googleapis.com/auth/spreadsheets'] });
                const sheets = google.sheets({ version: 'v4', auth });
                const response = await sheets.spreadsheets.values.get({ spreadsheetId: SPREADSHEET_ID, range: `${SHEET_NAME}!A4:S500` });
                const rows = response.data.values || [];
                let found = null;
                for (let row of rows) {
                    if (row[0] && row[0].toString().trim() === ep) {
                        found = { number: row[0].trim(), highlights: row[2] || 'لا توجد أحداث', rating: row[4] || 'غير مقيم', notes: row[6] || 'لا توجد ملاحظات', watched: row[8] === 'TRUE' || row[8] === true }; break;
                    }
                    if (row[18] && row[18].toString().trim() === ep) {
                        found = { number: row[18].trim(), highlights: row[16] || 'لا توجد أحداث', rating: row[14] || 'غير مقيم', notes: row[12] || 'لا توجد ملاحظات', watched: row[10] === 'TRUE' || row[10] === true }; break;
                    }
                }
                if (!found) return message.reply(`❌ لم يتم العثور على الحلقة **${ep}**.`);
                const embed = new EmbedBuilder().setTitle(`🎬 معلومات الحلقة: ${found.number}`).setColor(found.watched ? 0x2ECC71 : 0xE74C3C)
                    .addFields({ name: '⭐ التقييم', value: found.rating, inline: true }, { name: '📺 الحالة', value: found.watched ? '✅ تمت المشاهدة' : '❌ غير مشاهدة', inline: true }, { name: '🔍 أبرز الأحداث', value: found.highlights }, { name: '📝 الملاحظات', value: found.notes }).setTimestamp();
                return message.reply({ embeds: [embed] });
            } catch (err) {
                return message.reply(`❌ خطأ: ${err.message}`);
            }
        }

        // ----- أمر تحديث الأحداث !add_hdth -----
        if (command === 'add_hdth') {
            const parts = restOfContent.split('|').map(p => p.trim());
            if (parts.length < 2) return message.reply('❌ الاستخدام: `!add_hdth رقم الحلقة | النص الجديد`');
            const [ep, newText] = parts;
            try {
                const waitMsg = await message.reply('⏳ جاري تحديث الأحداث...');
                const auth = new google.auth.GoogleAuth({ keyFile: path.join(__dirname, 'credentials.json'), scopes: ['https://www.googleapis.com/auth/spreadsheets'] });
                const sheets = google.sheets({ version: 'v4', auth });
                const response = await sheets.spreadsheets.values.get({ spreadsheetId: SPREADSHEET_ID, range: `${SHEET_NAME}!A4:S500` });
                const rows = response.data.values || [];
                let cell = null;
                for (let i = 0; i < rows.length; i++) {
                    if (rows[i][0] && rows[i][0].toString().trim() === ep) { cell = `C${i + 4}`; break; }
                    if (rows[i][18] && rows[i][18].toString().trim() === ep) { cell = `Q${i + 4}`; break; }
                }
                if (!cell) return waitMsg.edit(`❌ لم يتم العثور على الحلقة **${ep}**.`);
                await sheets.spreadsheets.values.update({ spreadsheetId: SPREADSHEET_ID, range: `${SHEET_NAME}!${cell}`, valueInputOption: 'USER_ENTERED', resource: { values: [[newText]] } });
                await waitMsg.edit(`✅ تم تحديث أحداث الحلقة **${ep}** بنجاح.`);
                return message.delete().catch(() => {});
            } catch (err) {
                return message.reply(`❌ فشل: ${err.message}`);
            }
        }

        // ----- أمر تحديث أحداث فيلم !movie_hdth -----
        if (command === 'movie_hdth') {
            const parts = restOfContent.split('|').map(part => part.trim());
            if (parts.length < 2 || !parts[0] || !parts[1]) {
                return message.reply('❌ الاستخدام: `!movie_hdth رقم الفيلم | النص الجديد`');
            }
            const [movieNumber, ...textParts] = parts;
            const newText = textParts.join(' | ');
            try {
                const waitMsg = await message.reply('⏳ جاري تحديث أحداث الفيلم...');
                const result = await updateMovieHighlights(movieNumber, newText);
                await waitMsg.edit(`✅ تم تحديث أبرز أحداث الفيلم **${result.movie}** بنجاح.`);
                return message.delete().catch(() => {});
            } catch (err) {
                return message.reply(`❌ فشل تحديث أحداث الفيلم: ${err.message}`);
            }
        }

        // ----- أمر الترتيب !sort -----
        if (command === 'sort') {
            try {
                const waitMsg = await message.reply('⏳ جاري ترتيب الجدول...');
                await modifyAndSortEpisodes();
                await waitMsg.edit('✅ تم ترتيب الجدول بنجاح.');
                return message.delete().catch(() => {});
            } catch (err) {
                return message.reply(`❌ فشل: ${err.message}`);
            }
        }

        // ----- أمر التعليمات !commands -----
        if (command === 'commands') {
            const embed = new EmbedBuilder().setTitle('📖 دليل الأوامر').setColor(0xEE1D23).setThumbnail(conanClient.user.displayAvatarURL())
                .addFields({ name: '🕵️‍♂️ أوامر كونان', value: '• `!add رقم`: إضافة حلقة\n• `!remove رقم`: حذف حلقة\n• `!all`: عرض الكل\n• `!unwatched`: غير المشاهد\n• `!ep_info رقم`: معلومات الحلقة\n• `!add_hdth رقم | نص`: تحديث أحداث\n• `!movie_hdth رقم | نص`: تحديث أحداث فيلم\n• `!sort`: ترتيب الجدول\n• `!wiki رقم`: رابط الويكي' })
                .setTimestamp();
            return message.reply({ embeds: [embed] });
        }
    }

    // 2. الميزة القديمة: كتابة الرقم في قناة محددة
    if (message.channel.id === TARGET_CHANNEL_ID) {
        const contentRaw = message.content.trim();
        
        // التحقق من أن الرسالة تحتوي فقط على أرقام أو علامات + أو -
        const isValidInput = /^[0-9+\-]+$/.test(contentRaw);
        
        if (contentRaw.length > 0 && !contentRaw.startsWith(prefix) && isValidInput) {
            try {
                console.log(`[Sheet-Toggle] User: ${message.author.tag} | Episode: ${contentRaw}`);
                await updateConanSheet(contentRaw);
                await message.react('✅');
            } catch (err) {
                console.error(`Sheet Update Error: ${err.message}`);
            }
        }
    }

    // كتابة رقم الفيلم فقط في قناة m تزيد "عدد مرات التكرار" بمقدار واحد.
    if (message.channel.id === MOVIES_CHANNEL_ID) {
        const contentRaw = message.content.trim();
        const normalizedMovie = normalizeMovieNumber(contentRaw);
        if (contentRaw.length > 0 && !contentRaw.startsWith(prefix) && /^\d+$/.test(normalizedMovie)) {
            try {
                const newCount = await incrementMovieRepeatCount(normalizedMovie);
                await message.react('✅');
                console.log(`[Movie-Repeat] User: ${message.author.tag} | Movie: ${normalizedMovie} | Count: ${newCount}`);
            } catch (err) {
                console.error(`Movie repeat update error: ${err.message}`);
                await message.react('❌').catch(() => {});
            }
        }
    }
    });
}

// ==============================================================================
// 12. Watch List bot: Movies and Series
// ==============================================================================
const LIST_RANGE = 'A4:S1000';
const listPageSize = 10;

function cleanListTitle(value) {
    return String(value || '').trim().replace(/\s+/g, ' ').toLocaleLowerCase();
}

async function getListSheets() {
    if (!LIST_SPREADSHEET_ID) throw new Error('LISTSHEET_ID is missing from Render environment variables.');
    const auth = getGoogleAuth(['https://www.googleapis.com/auth/spreadsheets']);
    const sheets = google.sheets({ version: 'v4', auth });
    try {
        await sheets.spreadsheets.get({ spreadsheetId: LIST_SPREADSHEET_ID, fields: 'spreadsheetId' });
    } catch (error) {
        if (error.code === 404) {
            throw new Error('Google Sheets could not find LISTSHEET_ID, or the service-account editor has not been added to this spreadsheet. Check the ID and share the new sheet with the same service account as the Conan sheet.');
        }
        throw error;
    }
    return sheets;
}

async function getListTab(sheets, requested) {
    const metadata = await sheets.spreadsheets.get({ spreadsheetId: LIST_SPREADSHEET_ID, fields: 'sheets.properties.title' });
    const tab = (metadata.data.sheets || []).map(s => s.properties.title)
        .find(name => name.trim().toLocaleLowerCase() === requested.toLocaleLowerCase());
    if (!tab) throw new Error(`The ${requested} sheet was not found.`);
    return tab;
}

async function readListItems(kind) {
    const sheets = await getListSheets();
    const sheetName = await getListTab(sheets, kind === 'movies' ? LIST_MOVIES_SHEET : LIST_SERIES_SHEET);
    const response = await sheets.spreadsheets.values.get({ spreadsheetId: LIST_SPREADSHEET_ID, range: `${quoteSheetName(sheetName)}!${LIST_RANGE}` });
    const items = [];
    for (const [index, row] of (response.data.values || []).entries()) {
        const actualRow = index + 4;
        if (row[0]) items.push({ title: String(row[0]).trim(), row: actualRow, side: 'left', highlights: row[2] || '', rating: row[4] || '', notes: row[6] || '', watched: row[8] === true || row[8] === 'TRUE', status: row[8] || '' });
        if (row[18]) items.push({ title: String(row[18]).trim(), row: actualRow, side: 'right', highlights: row[16] || '', rating: row[14] || '', notes: row[12] || '', watched: row[10] === true || row[10] === 'TRUE', status: row[10] || '' });
    }
    return { sheets, sheetName, items };
}

async function findListItem(kind, title) {
    const data = await readListItems(kind);
    const wanted = cleanListTitle(title);
    const item = data.items.find(entry => cleanListTitle(entry.title) === wanted);
    if (!item) throw new Error(`"${title}" was not found in ${data.sheetName}. Use the exact title from the sheet.`);
    return { ...data, item };
}

async function addListItem(kind, title, highlights = '', rating = '', notes = '', status = '') {
    const data = await readListItems(kind);
    if (data.items.some(item => cleanListTitle(item.title) === cleanListTitle(title))) throw new Error(`"${title}" already exists.`);
    const response = await data.sheets.spreadsheets.values.get({ spreadsheetId: LIST_SPREADSHEET_ID, range: `${quoteSheetName(data.sheetName)}!A4:S1000` });
    const rows = response.data.values || [];
    let rowIndex = rows.findIndex(row => !String(row[0] || '').trim());
    let side = 'left';
    if (rowIndex === -1) { rowIndex = rows.findIndex(row => !String(row[18] || '').trim()); side = 'right'; }
    if (rowIndex === -1) { rowIndex = rows.length; side = 'left'; }
    const row = rowIndex + 4;
    const leftStatus = kind === 'movies' ? false : status.toUpperCase();
    const rightStatus = kind === 'movies' ? false : status.toUpperCase();
    const values = side === 'left'
        ? [[title, '', highlights, '', rating, '', notes, '', leftStatus]]
        : [[rightStatus, '', notes, '', rating, '', highlights, '', title]];
    const range = side === 'left' ? `A${row}:I${row}` : `K${row}:S${row}`;
    await data.sheets.spreadsheets.values.update({ spreadsheetId: LIST_SPREADSHEET_ID, range: `${quoteSheetName(data.sheetName)}!${range}`, valueInputOption: 'USER_ENTERED', resource: { values } });
    return { row, side };
}

async function toggleListMovie(title) {
    const found = await findListItem('movies', title);
    const column = found.item.side === 'left' ? 'I' : 'K';
    const watched = !found.item.watched;
    await found.sheets.spreadsheets.values.update({ spreadsheetId: LIST_SPREADSHEET_ID, range: `${quoteSheetName(found.sheetName)}!${column}${found.item.row}`, valueInputOption: 'USER_ENTERED', resource: { values: [[watched]] } });
    return { ...found.item, watched };
}

async function setListSeriesStatus(title, status) {
    if (!/^S\d+\.E\d+$/i.test(status)) throw new Error('Status must look like S1.E1.');
    const found = await findListItem('series', title);
    // الحالة sits in I for the left list and K for the right list.
    const column = found.item.side === 'left' ? 'I' : 'K';
    await found.sheets.spreadsheets.values.update({ spreadsheetId: LIST_SPREADSHEET_ID, range: `${quoteSheetName(found.sheetName)}!${column}${found.item.row}`, valueInputOption: 'USER_ENTERED', resource: { values: [[status.toUpperCase()]] } });
    return { ...found.item, status: status.toUpperCase() };
}

function listEmbed(kind, items, page) {
    const start = page * listPageSize;
    const batch = items.slice(start, start + listPageSize);
    const movie = kind === 'movies';
    return new EmbedBuilder().setColor(movie ? 0x5865F2 : 0x2ECC71)
        .setTitle(movie ? '🎬 Movies' : '📺 Series')
        .setDescription(batch.length ? batch.map((item, index) => `${start + index + 1}. **${item.title}** — ${movie ? (item.watched ? '✅ Watched' : '⬜ Not watched') : (item.status || 'No status')}`).join('\n') : 'No entries yet.')
        .setFooter({ text: `Page ${page + 1} of ${Math.max(1, Math.ceil(items.length / listPageSize))} • ${items.length} total` });
}

async function sendListPage(interaction, kind, page, update = false) {
    const { items } = await readListItems(kind);
    const maxPage = Math.max(0, Math.ceil(items.length / listPageSize) - 1);
    page = Math.max(0, Math.min(page, maxPage));
    const row = new ActionRowBuilder().addComponents(
        new ButtonBuilder().setCustomId(`list:${kind}:${page - 1}`).setLabel('Previous').setStyle(ButtonStyle.Secondary).setDisabled(page === 0),
        new ButtonBuilder().setCustomId(`list:${kind}:${page + 1}`).setLabel('Next').setStyle(ButtonStyle.Primary).setDisabled(page >= maxPage),
    );
    const payload = { embeds: [listEmbed(kind, items, page)], components: [row] };
    return update ? interaction.update(payload) : interaction.editReply(payload);
}

async function registerListCommands() {
    const commands = [
        new SlashCommandBuilder().setName('add_movie').setDescription('Add a movie to the Movies sheet').addStringOption(o => o.setName('title').setDescription('Exact movie title').setRequired(true)).addStringOption(o => o.setName('highlights').setDescription('Highlights')).addStringOption(o => o.setName('rating').setDescription('Rating')).addStringOption(o => o.setName('notes').setDescription('Notes')),
        new SlashCommandBuilder().setName('add_series').setDescription('Add a series to the Series sheet').addStringOption(o => o.setName('title').setDescription('Exact series title').setRequired(true)).addStringOption(o => o.setName('status').setDescription('Example: S1.E1')).addStringOption(o => o.setName('highlights').setDescription('Highlights')).addStringOption(o => o.setName('rating').setDescription('Rating')).addStringOption(o => o.setName('notes').setDescription('Notes')),
        new SlashCommandBuilder().setName('all_m').setDescription('View all movies, 10 per page'),
        new SlashCommandBuilder().setName('all_s').setDescription('View all series, 10 per page'),
        new SlashCommandBuilder().setName('imdb').setDescription('Open an IMDb search').addStringOption(o => o.setName('title').setDescription('Movie or series title').setRequired(true)),
        new SlashCommandBuilder().setName('movie_info').setDescription('View movie details').addStringOption(o => o.setName('title').setDescription('Exact movie title').setRequired(true)),
        new SlashCommandBuilder().setName('series_info').setDescription('View series details').addStringOption(o => o.setName('title').setDescription('Exact series title').setRequired(true)),
        new SlashCommandBuilder().setName('set_series_status').setDescription('Set a series status, e.g. S1.E1').addStringOption(o => o.setName('title').setDescription('Exact series title').setRequired(true)).addStringOption(o => o.setName('status').setDescription('Example: S1.E1').setRequired(true)),
    ].map(command => command.toJSON());
    const token = process.env.LIST_BOT_TOKEN;
    const clientId = process.env.LIST_BOT_CLIENT_ID;
    if (!token || !clientId) return console.error('[LIST BOT] LIST_BOT_TOKEN or LIST_BOT_CLIENT_ID is missing.');
    await new REST({ version: '10' }).setToken(token).put(Routes.applicationCommands(clientId), { body: commands });
    console.log('[LIST BOT] Slash commands registered.');
}

function attachListInteractionHandler() {
    listClient.on('interactionCreate', async interaction => {
        try {
            if (interaction.isButton() && interaction.customId.startsWith('list:')) {
                const [, kind, requestedPage] = interaction.customId.split(':');
                return sendListPage(interaction, kind, Number(requestedPage), true);
            }
            if (!interaction.isChatInputCommand()) return;
            if (interaction.commandName === 'all_m' || interaction.commandName === 'all_s') { await interaction.deferReply(); return sendListPage(interaction, interaction.commandName === 'all_m' ? 'movies' : 'series', 0); }
            if (interaction.commandName === 'imdb') { const title = interaction.options.getString('title'); return interaction.reply(`🔎 [Search IMDb for ${title}](https://www.imdb.com/find/?q=${encodeURIComponent(title)}&s=tt)`); }
            if (interaction.commandName === 'add_movie' || interaction.commandName === 'add_series') {
                await interaction.deferReply(); const kind = interaction.commandName === 'add_movie' ? 'movies' : 'series'; const title = interaction.options.getString('title');
                const result = await addListItem(kind, title, interaction.options.getString('highlights') || '', interaction.options.getString('rating') || '', interaction.options.getString('notes') || '', interaction.options.getString('status') || '');
                console.log(`[LIST BOT] Added ${kind}: ${title}`); return interaction.editReply(`✅ Added **${title}** to ${kind === 'movies' ? 'Movies' : 'Series'} (row ${result.row}).`);
            }
            if (interaction.commandName === 'set_series_status') { await interaction.deferReply(); const item = await setListSeriesStatus(interaction.options.getString('title'), interaction.options.getString('status')); console.log(`[LIST BOT] Series status: ${item.title} → ${item.status}`); return interaction.editReply(`✅ **${item.title}** is now ${item.status}.`); }
            if (interaction.commandName === 'movie_info' || interaction.commandName === 'series_info') { await interaction.deferReply(); const kind = interaction.commandName === 'movie_info' ? 'movies' : 'series'; const { item } = await findListItem(kind, interaction.options.getString('title')); return interaction.editReply({ embeds: [new EmbedBuilder().setColor(0x5865F2).setTitle(item.title).addFields({ name: 'Status', value: kind === 'movies' ? (item.watched ? '✅ Watched' : '⬜ Not watched') : (item.status || 'Not set'), inline: true }, { name: 'Rating', value: String(item.rating || '—'), inline: true }, { name: 'Highlights', value: String(item.highlights || '—') }, { name: 'Notes', value: String(item.notes || '—') })] }); }
        } catch (error) {
            console.error(`[LIST BOT] Interaction error (${interaction.commandName || interaction.customId}):`, error.code || '', error.message, error.stack || '');
            const message = `❌ ${error.message || 'The request could not be completed.'}`;
            if (interaction.deferred) return interaction.editReply(message).catch(replyError => console.error('[LIST BOT] Could not send error reply:', replyError.message));
            if (!interaction.replied) return interaction.reply({ content: message, ephemeral: true }).catch(replyError => console.error('[LIST BOT] Could not send error reply:', replyError.message));
        }
    });
}

function attachListMessageHandler() {
    listClient.on('messageCreate', async message => {
        if (message.author.bot) return;
        try {
            if (message.channel.id === LIST_MOVIES_CHANNEL_ID) { const item = await toggleListMovie(message.content.trim()); await message.react(item.watched ? '✅' : '↩️'); console.log(`[LIST BOT] Movie toggled: ${item.title} → ${item.watched}`); }
            if (message.channel.id === LIST_SERIES_CHANNEL_ID) { const match = message.content.trim().match(/^(.*?)\s+(S\d+\.E\d+)$/i); if (!match) return; const item = await setListSeriesStatus(match[1], match[2]); await message.react('✅'); console.log(`[LIST BOT] Series status: ${item.title} → ${item.status}`); }
        } catch (error) { console.error('[LIST BOT] Message update error:', error.message); await message.react('❌').catch(() => {}); }
    });
}

// ==============================================================================
// 13. نظام معالجة الأخطاء والدخول (Error & Login Handling)
// ==============================================================================

// هذه الدوال مسؤولة عن حماية البوت من التوقف الكامل إذا حدث خطأ ما لم نحسب حسابه في الأكواد
process.on('unhandledRejection', (reason, promise) => {
    console.error('❌ Unhandled Rejection at:', promise, 'reason:', reason);
});

process.on('uncaughtException', (err) => {
    console.error('❌ Uncaught Exception:', err);
    // نقوم بالخروج ليسمح لنظام PM2 بإعادة تشغيله تلقائياً وبشكل نظيف.
    process.exit(1);
});

// أخيراً: إنشاء البوتات وتسجيل الدخول
// (يسحب معلومات الدخول من ملف config.json أو Environment Variables)
const generalToken = process.env.GENERAL_BOT_TOKEN || config.generalBot?.token;
const conanToken = process.env.CONAN_BOT_TOKEN || config.conanBot?.token;
const listToken = process.env.LIST_BOT_TOKEN;

if (generalToken) {
    setupGeneralBot();
    generalClient.login(generalToken).catch(err => console.error('General Bot login error:', err));
} else {
    console.error('[STARTUP] General Assistant was not started because GENERAL_BOT_TOKEN is missing. Add it in Render Environment variables.');
}
if (conanToken) {
    setupConanBot();
    conanClient.login(conanToken).catch(err => console.error('Conan Bot login error:', err));
} else {
    console.error('[STARTUP] gen2 bot was not started because CONAN_BOT_TOKEN is missing. Add it in Render Environment variables.');
}
if (listToken) {
    setupListBot();
    listClient.login(listToken).catch(err => console.error('Watch List bot login error:', err));
} else {
    console.error('[STARTUP] Watch List bot was not started because LIST_BOT_TOKEN is missing.');
}

// --- Express Server & Static Assets ---
app.use(express.static(mainDir)); // تشغيل ملفات الموقع الرئيسي (index.css, script.js, إلخ)

// --- مسارات OAuth الخاصة بـ eWeLink ---
app.get('/api/ewelink/oauth', (req, res) => {
    const appId = process.env.EWELINK_APP_ID;
    const appSecret = process.env.EWELINK_APP_SECRET;
    
    if (!appId || !appSecret) {
        return res.status(500).send("EWELINK_APP_ID or EWELINK_APP_SECRET is missing.");
    }

    const client = new Ewelink.WebAPI({ appId, appSecret });
    const host = (req.get('x-forwarded-host') || req.get('host') || '').replace(/^www\./i, '');
    const callbackUrl = process.env.EWELINK_REDIRECT_URL || `https://${host}/api/ewelink/callback`;
    
    // Generate the official login URL using the library's helper
    // This automatically includes nonce, seq, and correctly formatted parameters
    const loginUrl = client.oauth.createLoginUrl({
        redirectUrl: callbackUrl,
        grantType: 'authorization_code',
        state: Math.random().toString(36).substring(2, 12) // Random 10-char state
    });

    console.log(`📡 [EWELINK] Redirecting user to official login URL with redirectUrl: ${callbackUrl}`);
    res.redirect(loginUrl);
});

app.get('/api/ewelink/callback', async (req, res) => {
    const { code, region } = req.query;
    if (!code) return res.status(400).send("Authorization code missing.");

    try {
        const appId = process.env.EWELINK_APP_ID;
        const appSecret = process.env.EWELINK_APP_SECRET;
        const host = (req.get('x-forwarded-host') || req.get('host') || '').replace(/^www\./i, '');
        const callbackUrl = process.env.EWELINK_REDIRECT_URL || `https://${host}/api/ewelink/callback`;
        
        const client = new Ewelink.WebAPI({ appId, appSecret });

        console.log(`📡 [EWELINK] Exchanging authorization code for tokens (Region: ${region || 'eu'}, Callback: ${callbackUrl})...`);
        
        // Use the library's built-in token exchange helper
        const tokenRes = await client.oauth.getToken({
            region: region || 'eu',
            redirectUrl: callbackUrl,
            code: code
        });

        if (tokenRes.error && tokenRes.error !== 0) {
            console.error('❌ [EWELINK] Token Exchange Error:', tokenRes);
            return res.status(400).send(`eWeLink OAuth Error: ${tokenRes.msg || tokenRes.error}`);
        }

        // --- BULLETPROOF TOKEN SCOUT ---
        // We search the entire object for the access token no matter what eWeLink calls it.
        const rawData = tokenRes.data || tokenRes;
        console.log(`[DEBUG] Scouting token in keys: ${Object.keys(rawData).join(', ')}`);
        
        const extractedAt = rawData.access_token || rawData.accessToken || rawData.at || rawData.token;
        const extractedRt = rawData.refresh_token || rawData.refreshToken || rawData.rt;

        if (!extractedAt) {
            console.error("❌ [EWELINK] FAILED: Could not find any access token in the raw response!");
            console.error("[DEBUG] Full Raw Keys:", Object.keys(rawData));
            return res.status(500).send("Criticial Error: eWeLink returned a success page but did not send a valid token. Please check system logs.");
        }

        console.log(`✅ [EWELINK] Token found! Length: ${extractedAt.length}, Type: ${typeof extractedAt}`);
        
        // حفظ التوكن
        const tokenData = {
            at: extractedAt,
            rt: extractedRt,
            region: region || 'eu',
            expires: Date.now() + (30 * 24 * 60 * 60 * 1000) // 30 days
        };
        
        fs.writeFileSync('./ewelink_token.json', JSON.stringify(tokenData));
        console.log("✅ [EWELINK] OAuth Token successfully saved to disk.");
        
        // إعادة تهيئة eWeLink للبدء في التحكم بالأجهزة
        // Pass the tokenData DIRECTLY to bypass disk delay and ensure the new token is used immediately
        await initEwelink(tokenData);
        
        res.send("<html><body><h2 style='color: #10b981;'>✅ eWeLink Connected Successfully!</h2><p>You can close this tab and refresh the dashboard.</p><script>setTimeout(()=>window.location.href='/dis', 3000);</script></body></html>");
    } catch (err) {
        console.error('❌ [EWELINK] OAuth Callback Error:', err);
        res.status(500).send(`Internal Server Error during OAuth callback: ${err.message}`);
    }
});

// جلب قائمة الأجهزة للداشبورد
app.get('/api/ewelink/devices', (req, res) => {
    res.set('Cache-Control', 'no-store, no-cache, must-revalidate, private');
    if (!ewelinkConnection) return res.status(503).json({ error: 'eWeLink not connected. Please click Setup eWeLink.' });
    
    // We send an array to the UI
    res.json(ewelinkDevices.map(d => ({
        deviceid: d.itemData.deviceid,
        name: d.itemData.name,
        params: d.itemData.params,
        online: d.itemData.online
    })));
});


// التحكم في الأجهزة من الداشبورد
app.post('/api/ewelink/action', express.json(), async (req, res) => {
    let { deviceid, action } = req.body;
    if (!ewelinkConnection || !ewelinkConnection.at) return res.status(503).json({ error: 'eWeLink not connected' });
    
    try {
        // --- SMART TOGGLE LOGIC ---
        if (action === 'turn') {
            const dev = ewelinkDevices.find(d => d.itemData.deviceid === deviceid);
            const currentStatus = dev?.itemData?.params?.switch || 'off';
            action = currentStatus === 'on' ? 'off' : 'on';
        }

        console.log(`[DASHBOARD] Executing action: ${action} on ${deviceid}`);
        
        const response = await manualSetDeviceStatus(
            ewelinkConnection.region, 
            ewelinkConnection.at, 
            deviceid, 
            action
        );

        if (response && response.error && response.error !== 0) {
            throw new Error(`Device action failed: ${response.msg || response.error}`);
        }

        // تحديث الحالة محلياً
        const dev = ewelinkDevices.find(d => d.itemData.deviceid === deviceid);
        if (dev) {
            if (!dev.itemData.params) dev.itemData.params = {};
            dev.itemData.params.switch = action;
        }
        
        res.json({ success: true, newState: action });
    } catch (err) {
        console.error('❌ [ACTION ERROR]:', err.message);
        res.status(500).json({ error: err.message });
    }
});

// نظام الـ Keep-alive وتواجد البوتات
app.get('/api/status', (req, res) => {
    res.json({
        general: generalClient?.isReady() ? 'Online' : 'Offline',
        conan: conanClient?.isReady() ? 'Online' : 'Offline',
        lists: listClient?.isReady() ? 'Online' : 'Offline',
        logs: recentLogs
    });
});

// أي رابط غير معرّف، مثل /a، يعرض خطأ 404 صريحاً بدلاً من صفحة فارغة.
app.use((req, res) => {
    res.status(404).type('text').send('404 - Page not found');
});

app.listen(port, () => {
    console.log(`🚀 Server & Dashboard listening on port ${port}`);
});
