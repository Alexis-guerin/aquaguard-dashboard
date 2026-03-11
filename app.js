// ===== CONFIGURATION SUPABASE =====
const SUPABASE_URL = 'https://fhauqbpgrwunmzzeqylj.supabase.co';
const SUPABASE_KEY = 'sb_publishable_UGdWt_CWd-jPWRj0_vegKA_gAIbNKb_';

// "supabaseClient" pour éviter le conflit avec window.supabase
const supabaseClient = window.supabase.createClient(SUPABASE_URL, SUPABASE_KEY);

// ===== GRAPHIQUE =====
const canvas = document.getElementById('chart');
const ctx = canvas.getContext('2d');
let chartData = { x: [], y: [], z: [], timestamps: [] };
const MAX_POINTS = 100;

// ===== INIT =====
document.addEventListener('DOMContentLoaded', () => {
    console.log('🚀 MAVANS AQUA-GUARD - Système démarré');
    loadLatestData();
    loadAlerts();
    setupRealtimeSubscription();
    setupAlertsSubscription();
});

// ===== CHARGER DONNÉES =====
async function loadLatestData() {
    try {
        const { data, error } = await supabaseClient
            .from('sensor_data')
            .select('*')
            .order('timestamp', { ascending: false })
            .limit(MAX_POINTS);

        if (error) throw error;

        if (data && data.length > 0) {
            data.reverse().forEach(r => updateChartData(r));
            updateDisplay(data[data.length - 1]);
            drawChart();
            document.getElementById('status-text').textContent = 'Système opérationnel';
            console.log(`✅ ${data.length} enregistrements chargés`);
        }
    } catch (err) {
        console.error('❌ Erreur chargement:', err);
        document.getElementById('status-text').textContent = 'Erreur de connexion';
    }
}

// ===== CHARGER ALERTES =====
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
        container.innerHTML = '';

        if (data && data.length > 0) {
            data.forEach(a => addAlert(a));
        } else {
            container.innerHTML = '<div class="no-data">✅ Aucune alerte active</div>';
        }
    } catch (err) {
        console.error('❌ Erreur alertes:', err);
    }
}

// ===== TEMPS RÉEL - DONNÉES =====
function setupRealtimeSubscription() {
    supabaseClient
        .channel('sensor_data_changes')
        .on('postgres_changes',
            { event: 'INSERT', schema: 'public', table: 'sensor_data' },
            ({ new: row }) => {
                console.log('📡 Nouvelle donnée:', row);
                updateDisplay(row);
                updateChartData(row);
                drawChart();
            }
        )
        .subscribe();
}

// ===== TEMPS RÉEL - ALERTES =====
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
    // Accéléromètre
    document.getElementById('accel-x').textContent = d.accel_x.toFixed(2) + ' m/s²';
    document.getElementById('accel-y').textContent = d.accel_y.toFixed(2) + ' m/s²';
    document.getElementById('accel-z').textContent = d.accel_z.toFixed(2) + ' m/s²';

    // Gyroscope
    document.getElementById('gyro-x').textContent = d.gyro_x.toFixed(2) + ' °/s';
    document.getElementById('gyro-y').textContent = d.gyro_y.toFixed(2) + ' °/s';
    document.getElementById('gyro-z').textContent = d.gyro_z.toFixed(2) + ' °/s';

    // Température air
    document.getElementById('temp').textContent = d.temperature.toFixed(1) + '°C';

    // Température eau
    if (d.water_temperature != null)
        document.getElementById('water-temp').textContent = d.water_temperature.toFixed(1) + '°C';

    // Batterie
    if (d.battery_level != null) {
        const b = d.battery_level;
        document.getElementById('battery-value').textContent = b.toFixed(1) + '%';
        document.getElementById('battery-text').textContent = b.toFixed(0) + '%';
        const bar = document.getElementById('battery-bar');
        bar.style.height = b + '%';
        bar.classList.remove('low', 'medium');
        if (b < 20)      bar.classList.add('low');
        else if (b < 50) bar.classList.add('medium');
    }

    // Humidité
    if (d.humidity != null)
        document.getElementById('humidity').textContent = d.humidity.toFixed(1) + '%';

    // Conductivité & TDS
    if (d.conductivity != null)
        document.getElementById('conductivity').textContent = d.conductivity.toFixed(1) + ' µS/cm';

    if (d.tds != null) {
        document.getElementById('tds').textContent = d.tds.toFixed(1) + ' ppm';
        let quality = 'Excellent';
        if (d.tds > 500)  quality = 'Moyen';
        if (d.tds > 1000) quality = 'Mauvais';
        document.getElementById('water-quality').textContent = quality;
    }

    // GPS
    if (d.latitude && d.longitude) {
        document.getElementById('gps-lat').textContent = 'Latitude: '  + d.latitude.toFixed(6)  + '°';
        document.getElementById('gps-lon').textContent = 'Longitude: ' + d.longitude.toFixed(6) + '°';
    }

    // Timestamp
    document.getElementById('accel-time').textContent =
        new Date(d.timestamp).toLocaleTimeString('fr-FR');
}

// ===== AJOUTER ALERTE =====
function addAlert(alert) {
    const container = document.getElementById('alerts-container');
    if (container.querySelector('.no-data')) container.innerHTML = '';

    const div = document.createElement('div');
    div.className = 'alert';
    div.innerHTML = `
        <div class="alert-header">
            <span class="alert-type">${alert.alert_type.replace(/_/g, ' ')}</span>
            <span class="alert-time">${new Date(alert.timestamp).toLocaleString('fr-FR')}</span>
        </div>
        <div class="alert-message">${alert.message}</div>
    `;
    container.insertBefore(div, container.firstChild);

    while (container.children.length > 10)
        container.removeChild(container.lastChild);
}

// ===== GRAPHIQUE =====
function updateChartData(d) {
    chartData.x.push(d.accel_x);
    chartData.y.push(d.accel_y);
    chartData.z.push(d.accel_z);
    chartData.timestamps.push(new Date(d.timestamp));

    if (chartData.x.length > MAX_POINTS) {
        chartData.x.shift();
        chartData.y.shift();
        chartData.z.shift();
        chartData.timestamps.shift();
    }
}

function drawChart() {
    const w = canvas.offsetWidth;
    const h = 280;
    canvas.width = w;
    canvas.height = h;
    ctx.clearRect(0, 0, w, h);

    if (chartData.x.length === 0) return;

    const pad = 45;
    const cw = w - 2 * pad;
    const ch = h - 2 * pad;
    const zeroY = pad + ch / 2;

    // Grille
    ctx.strokeStyle = 'rgba(255,255,255,0.04)';
    ctx.lineWidth = 1;
    for (let i = 0; i <= 8; i++) {
        const y = pad + (ch / 8) * i;
        ctx.beginPath();
        ctx.moveTo(pad, y);
        ctx.lineTo(w - pad, y);
        ctx.stroke();
    }

    // Ligne zéro
    ctx.strokeStyle = 'rgba(212,175,55,0.3)';
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(pad, zeroY);
    ctx.lineTo(w - pad, zeroY);
    ctx.stroke();

    // Courbes
    const colors = { x: '#ef4444', y: '#10b981', z: '#3b82f6' };

    ['x', 'y', 'z'].forEach(axis => {
        if (chartData[axis].length < 2) return;
        ctx.strokeStyle = colors[axis];
        ctx.lineWidth = 3;
        ctx.shadowBlur = 12;
        ctx.shadowColor = colors[axis];
        ctx.beginPath();

        chartData[axis].forEach((val, i) => {
            const x = pad + (cw / (MAX_POINTS - 1)) * i;
            const norm = Math.max(-20, Math.min(20, val));
            const y = zeroY - (norm / 20) * (ch / 2);
            i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
        });

        ctx.stroke();
        ctx.shadowBlur = 0;
    });

    // Légende
    ctx.font = 'bold 14px Arial';
    let lx = 15;
    ['x', 'y', 'z'].forEach(axis => {
        ctx.fillStyle = colors[axis];
        ctx.fillText(axis.toUpperCase(), lx, 25);
        lx += 35;
    });
}

window.addEventListener('resize', drawChart);
