// ===== CONFIGURATION =====
const CONFIG = {
    SUPABASE_URL: 'https://fhauqbpgrwunmzzeqylj.supabase.co',
    SUPABASE_KEY: 'sb_publishable_UGdWt_CWd-jPWRj0_vegKA_gAIbNKb_', // Remplacez par votre clé
    MAX_CHART_POINTS: 100,
    EFFICACITE_GALET: 0.15
};

// ===== INITIALISATION SUPABASE =====
const supabaseClient = window.supabase.createClient(CONFIG.SUPABASE_URL, CONFIG.SUPABASE_KEY);

// ===== ÉTAT GLOBAL =====
const state = {
    theme: localStorage.getItem('theme') || 'dark',
    latestData: null,
    chartData: { x: [], y: [], z: [], timestamps: [] },
    thresholds: JSON.parse(localStorage.getItem('thresholds')) || {
        phMin: 6.5,
        phMax: 9.0,
        tempMin: 10,
        tempMax: 28,
        battery: 20,
        humidity: 85
    }
};

// ===== INITIALISATION =====
document.addEventListener('DOMContentLoaded', () => {
    console.log('🚀 MAVANS AQUA-GUARD v2.0 — Système démarré');
    
    initTheme();
    initTabs();
    initThresholdSliders();
    initCalculator();
    
    loadLatestData();
    loadAlerts();
    setupRealtimeSubscription();
    setupAlertsSubscription();
});

// ===== THÈME SOMBRE / CLAIR =====
function initTheme() {
    applyTheme(state.theme);
    
    const toggle = document.getElementById('theme-toggle');
    const select = document.getElementById('theme-select');
    
    if (toggle) {
        toggle.addEventListener('click', () => {
            state.theme = state.theme === 'dark' ? 'light' : 'dark';
            applyTheme(state.theme);
        });
    }
    
    if (select) {
        select.value = state.theme;
        select.addEventListener('change', (e) => {
            state.theme = e.target.value;
            applyTheme(state.theme);
        });
    }
}

function applyTheme(theme) {
    document.documentElement.setAttribute('data-theme', theme);
    localStorage.setItem('theme', theme);
    
    const toggle = document.getElementById('theme-toggle');
    if (toggle) toggle.textContent = theme === 'dark' ? '🌙' : '☀️';
    
    const select = document.getElementById('theme-select');
    if (select) select.value = theme;
}

// ===== ONGLETS =====
function initTabs() {
    const tabBtns = document.querySelectorAll('.tab-btn');
    
    tabBtns.forEach(btn => {
        btn.addEventListener('click', () => {
            const tabId = btn.dataset.tab;
            
            // Désactiver tous les onglets
            tabBtns.forEach(b => b.classList.remove('active'));
            document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
            
            // Activer l'onglet sélectionné
            btn.classList.add('active');
            document.getElementById(`tab-${tabId}`).classList.add('active');
            
            // Redessiner le graphique si on revient au dashboard
            if (tabId === 'dashboard') drawChart();
        });
    });
}

// ===== CHARGEMENT DES DONNÉES =====
async function loadLatestData() {
    try {
        const { data, error } = await supabaseClient
            .from('sensor_data')
            .select('*')
            .order('timestamp', { ascending: false })
            .limit(CONFIG.MAX_CHART_POINTS);

        if (error) throw error;

        if (data && data.length > 0) {
            data.reverse().forEach(r => updateChartData(r));
            state.latestData = data[data.length - 1];
            updateDisplay(state.latestData);
            updateCalculatorAutoValues(state.latestData);
            drawChart();
            
            setStatus('Système opérationnel', true);
            console.log(`✅ ${data.length} enregistrements chargés`);
        }
    } catch (err) {
        console.error('❌ Erreur chargement:', err);
        setStatus('Erreur de connexion', false);
    }
}

async function loadAlerts() {
    try {
        const { data, error } = await supabaseClient
            .from('alerts')
            .select('*')
            .eq('resolved', false)
            .order('timestamp', { ascending: false })
            .limit(10);

        if (error) throw error;

        const container = document.getElementById('alerts-container');
        if (!container) return;
        
        container.innerHTML = '';

        if (data && data.length > 0) {
            data.forEach(a => addAlert(a));
        } else {
            container.innerHTML = '<div class="no-alert">✅ Aucune alerte active</div>';
        }
    } catch (err) {
        console.error('❌ Erreur alertes:', err);
    }
}

// ===== TEMPS RÉEL =====
function setupRealtimeSubscription() {
    supabaseClient
        .channel('sensor_data_changes')
        .on('postgres_changes',
            { event: 'INSERT', schema: 'public', table: 'sensor_data' },
            ({ new: row }) => {
                console.log('📡 Nouvelle donnée:', row);
                state.latestData = row;
                updateDisplay(row);
                updateCalculatorAutoValues(row);
                updateChartData(row);
                drawChart();
                checkThresholds(row);
            }
        )
        .subscribe();
}

function setupAlertsSubscription() {
    supabaseClient
        .channel('alerts_changes')
        .on('postgres_changes',
            { event: 'INSERT', schema: 'public', table: 'alerts' },
            ({ new: row }) => {
                console.log('🚨 Nouvelle alerte:', row);
                addAlert(row);
            }
        )
        .subscribe();
}

// ===== MISE À JOUR AFFICHAGE =====
function updateDisplay(d) {
    // Helper pour mettre à jour un élément
    const set = (id, value) => {
        const el = document.getElementById(id);
        if (el) el.textContent = value;
    };

    // Accéléromètre
    set('accel-x', d.accel_x?.toFixed(2) + ' m/s²');
    set('accel-y', d.accel_y?.toFixed(2) + ' m/s²');
    set('accel-z', d.accel_z?.toFixed(2) + ' m/s²');

    // Gyroscope
    set('gyro-x', d.gyro_x?.toFixed(2) + ' °/s');
    set('gyro-y', d.gyro_y?.toFixed(2) + ' °/s');
    set('gyro-z', d.gyro_z?.toFixed(2) + ' °/s');

    // Températures
    set('temp', d.temperature?.toFixed(1) + '°C');
    set('water-temp', d.water_temperature?.toFixed(1) + '°C');

    // pH
    if (d.ph != null) {
        set('ph', d.ph.toFixed(1));
        const cursor = document.getElementById('ph-cursor');
        if (cursor) cursor.style.left = ((d.ph / 14) * 100) + '%';
        
        const status = document.getElementById('ph-status');
        if (status) {
            if (d.ph < 6.5) status.textContent = '⚠️ Eau acide';
            else if (d.ph >= 7.8 && d.ph <= 8.3) status.textContent = '✅ Normal (eau de mer)';
            else if (d.ph > 8.3 && d.ph <= 9) status.textContent = '⚠️ Légèrement basique';
            else if (d.ph > 9) status.textContent = '🚨 Très basique';
            else status.textContent = '✅ Acceptable';
        }
    }

    // Batterie
    if (d.battery_level != null) {
        const b = d.battery_level;
        set('battery-value', b.toFixed(0) + '%');
        
        const bar = document.getElementById('battery-bar');
        if (bar) {
            bar.style.height = b + '%';
            bar.classList.remove('low', 'medium');
            if (b < 20) bar.classList.add('low');
            else if (b < 50) bar.classList.add('medium');
        }
    }

    // Humidité
    set('humidity', d.humidity?.toFixed(1) + '%');

    // Conductivité & TDS
    set('conductivity', d.conductivity?.toFixed(1) + ' µS/cm');
    set('tds', d.tds?.toFixed(1) + ' ppm');
    
    if (d.tds != null) {
        let quality = 'Excellent';
        if (d.tds > 500) quality = 'Moyen';
        if (d.tds > 1000) quality = 'Mauvais';
        set('water-quality', quality);
    }

    // GPS
    if (d.latitude && d.longitude) {
        set('gps-lat', d.latitude.toFixed(5) + '°');
        set('gps-lon', d.longitude.toFixed(5) + '°');
        
        const link = document.getElementById('maps-link');
        if (link) link.href = `https://www.google.com/maps?q=${d.latitude},${d.longitude}`;
    }

    // Timestamp
    set('accel-time', new Date(d.timestamp).toLocaleTimeString('fr-FR'));
    set('last-sync', new Date(d.timestamp).toLocaleString('fr-FR'));
}

function setStatus(text, success) {
    const el = document.getElementById('status-text') || document.getElementById('status-txt');
    if (el) el.textContent = text;
}

// ===== ALERTES =====
function addAlert(alert) {
    const container = document.getElementById('alerts-container');
    if (!container) return;
    
    const noAlert = container.querySelector('.no-alert, .loading');
    if (noAlert) container.innerHTML = '';

    const emojis = {
        temperature_haute: '🌡️',
        temp_eau_elevee: '🌊',
        mouvement_anormal: '⚠️',
        humidite_elevee: '💧',
        batterie_faible: '🔋',
        conductivite_anormale: '⚡',
        ph_anormal: '🧪',
    };

    const div = document.createElement('div');
    div.className = 'alert-item';
    div.innerHTML = `
        <div class="alert-emoji">${emojis[alert.alert_type] || '🚨'}</div>
        <div class="alert-body">
            <div class="alert-title">${alert.alert_type.replace(/_/g, ' ')}</div>
            <div class="alert-msg">${alert.message}</div>
            <div class="alert-time">${new Date(alert.timestamp).toLocaleString('fr-FR')}</div>
        </div>
    `;
    container.insertBefore(div, container.firstChild);

    // Limiter à 10 alertes
    while (container.children.length > 10) {
        container.removeChild(container.lastChild);
    }
}

// ===== GRAPHIQUE =====
function updateChartData(d) {
    state.chartData.x.push(d.accel_x);
    state.chartData.y.push(d.accel_y);
    state.chartData.z.push(d.accel_z);
    state.chartData.timestamps.push(new Date(d.timestamp));

    if (state.chartData.x.length > CONFIG.MAX_CHART_POINTS) {
        state.chartData.x.shift();
        state.chartData.y.shift();
        state.chartData.z.shift();
        state.chartData.timestamps.shift();
    }
}

function drawChart() {
    const canvas = document.getElementById('chart');
    if (!canvas) return;
    
    const ctx = canvas.getContext('2d');
    const w = canvas.offsetWidth || 600;
    const h = 250;
    
    canvas.width = w;
    canvas.height = h;
    ctx.clearRect(0, 0, w, h);

    if (state.chartData.x.length === 0) return;

    const pad = 40;
    const cw = w - 2 * pad;
    const ch = h - 2 * pad;
    const zeroY = pad + ch / 2;

    // Grille
    ctx.strokeStyle = 'rgba(255,255,255,0.05)';
    ctx.lineWidth = 1;
    for (let i = 0; i <= 6; i++) {
        const y = pad + (ch / 6) * i;
        ctx.beginPath();
        ctx.moveTo(pad, y);
        ctx.lineTo(w - pad, y);
        ctx.stroke();
    }

    // Ligne zéro
    ctx.strokeStyle = 'rgba(212,175,55,0.3)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(pad, zeroY);
    ctx.lineTo(w - pad, zeroY);
    ctx.stroke();

    // Courbes
    const colors = { x: '#ef4444', y: '#10b981', z: '#3b82f6' };

    ['x', 'y', 'z'].forEach(axis => {
        if (state.chartData[axis].length < 2) return;
        
        ctx.strokeStyle = colors[axis];
        ctx.lineWidth = 2;
        ctx.beginPath();

        state.chartData[axis].forEach((val, i) => {
            const x = pad + (cw / (CONFIG.MAX_CHART_POINTS - 1)) * i;
            const norm = Math.max(-20, Math.min(20, val));
            const y = zeroY - (norm / 20) * (ch / 2);
            i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
        });

        ctx.stroke();
    });

    // Légende
    ctx.font = 'bold 12px Arial';
    let lx = 10;
    ['X', 'Y', 'Z'].forEach((label, i) => {
        ctx.fillStyle = Object.values(colors)[i];
        ctx.fillText(label, lx, 18);
        lx += 30;
    });
}

window.addEventListener('resize', drawChart);

// ===== CALCULATEUR DE GALETS =====
function initCalculator() {
    const btn = document.getElementById('btn-calculate');
    if (btn) {
        btn.addEventListener('click', calculateGalets);
    }
}

function updateCalculatorAutoValues(d) {
    const setAuto = (id, value) => {
        const el = document.getElementById(id);
        if (el) el.textContent = value;
    };
    
    if (d.ph != null) setAuto('calc-ph-auto', d.ph.toFixed(1));
    if (d.water_temperature != null) setAuto('calc-temp-auto', d.water_temperature.toFixed(1) + '°C');
    if (d.conductivity != null) setAuto('calc-cond-auto', d.conductivity.toFixed(0) + ' µS/cm');
}

function calculateGalets() {
    const phActuel = parseFloat(document.getElementById('calc-ph').value);
    const phCible = parseFloat(document.getElementById('calc-ph-target').value);
    const temp = parseFloat(document.getElementById('calc-temp').value);
    const cond = parseFloat(document.getElementById('calc-cond').value);
    const taille = parseInt(document.getElementById('calc-bourriche').value);

    // Validation
    if (isNaN(phActuel) || isNaN(phCible) || isNaN(temp) || isNaN(cond)) {
        alert('Veuillez remplir tous les champs');
        return;
    }

    const delta = phCible - phActuel;
    
    const resultBox = document.getElementById('calc-result');
    const resultValue = document.getElementById('calc-result-value');
    const resultDetail = document.getElementById('calc-result-detail');

    if (delta <= 0) {
        resultBox.style.display = 'block';
        resultBox.className = 'result-box';
        resultValue.textContent = '0';
        resultDetail.textContent = 'Le pH actuel est déjà égal ou supérieur au pH cible. Aucun galet nécessaire.';
        return;
    }

    // Coefficients
    const coeffVolume = { 2: 1.0, 3: 1.5, 5: 2.5, 10: 5.0 }[taille] || 1.0;
    const coeffTemp = 1.0 + (temp - 15) * 0.02;
    const coeffCond = 1.0 - (cond / 100000);

    // Calcul
    const nbGalets = Math.ceil(delta * coeffVolume * coeffTemp * coeffCond / CONFIG.EFFICACITE_GALET);

    resultBox.style.display = 'block';
    resultBox.className = 'result-box warning';
    resultValue.textContent = nbGalets;
    resultDetail.textContent = `Pour corriger le pH de ${phActuel.toFixed(1)} → ${phCible.toFixed(1)} dans une bourriche de ${taille} kg`;
}

// ===== PARAMÈTRES & SEUILS =====
function initThresholdSliders() {
    const sliders = [
        { id: 'threshold-ph-min', key: 'phMin', suffix: '' },
        { id: 'threshold-ph-max', key: 'phMax', suffix: '' },
        { id: 'threshold-temp-min', key: 'tempMin', suffix: '°C' },
        { id: 'threshold-temp-max', key: 'tempMax', suffix: '°C' },
        { id: 'threshold-battery', key: 'battery', suffix: '%' },
        { id: 'threshold-humidity', key: 'humidity', suffix: '%' }
    ];

    sliders.forEach(({ id, key, suffix }) => {
        const slider = document.getElementById(id);
        if (!slider) return;
        
        slider.value = state.thresholds[key];
        updateSliderDisplay(id, state.thresholds[key], suffix);

        slider.addEventListener('input', (e) => {
            const val = parseFloat(e.target.value);
            state.thresholds[key] = val;
            updateSliderDisplay(id, val, suffix);
        });
    });

    // Bouton sauvegarder
    const btnSave = document.getElementById('btn-save-settings');
    if (btnSave) {
        btnSave.addEventListener('click', () => {
            localStorage.setItem('thresholds', JSON.stringify(state.thresholds));
            alert('✅ Paramètres sauvegardés !');
        });
    }

    // Bouton reset
    const btnReset = document.getElementById('btn-reset-settings');
    if (btnReset) {
        btnReset.addEventListener('click', () => {
            state.thresholds = {
                phMin: 6.5, phMax: 9.0,
                tempMin: 10, tempMax: 28,
                battery: 20, humidity: 85
            };
            localStorage.setItem('thresholds', JSON.stringify(state.thresholds));
            initThresholdSliders();
            alert('🔄 Paramètres réinitialisés');
        });
    }
}

function updateSliderDisplay(sliderId, value, suffix) {
    const valId = sliderId.replace('threshold-', 'val-');
    const badgeId = sliderId.replace('threshold-', 'badge-');
    
    const valEl = document.getElementById(valId);
    const badgeEl = document.getElementById(badgeId);
    
    if (valEl) valEl.textContent = value;
    if (badgeEl) badgeEl.textContent = value + suffix;
}

function checkThresholds(d) {
    const t = state.thresholds;
    const alerts = [];

    if (d.ph != null) {
        if (d.ph < t.phMin) alerts.push(`pH trop bas: ${d.ph.toFixed(1)}`);
        if (d.ph > t.phMax) alerts.push(`pH trop élevé: ${d.ph.toFixed(1)}`);
    }

    if (d.water_temperature != null) {
        if (d.water_temperature < t.tempMin) alerts.push(`Eau trop froide: ${d.water_temperature.toFixed(1)}°C`);
        if (d.water_temperature > t.tempMax) alerts.push(`Eau trop chaude: ${d.water_temperature.toFixed(1)}°C`);
    }

    if (d.battery_level != null && d.battery_level < t.battery) {
        alerts.push(`Batterie faible: ${d.battery_level.toFixed(0)}%`);
    }

    if (d.humidity != null && d.humidity > t.humidity) {
        alerts.push(`Humidité élevée: ${d.humidity.toFixed(0)}%`);
    }

    // Log des alertes locales (vous pouvez les envoyer à Supabase)
    if (alerts.length > 0) {
        console.warn('⚠️ Seuils dépassés:', alerts);
    }
}
