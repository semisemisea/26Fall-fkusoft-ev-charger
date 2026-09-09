/* 腾讯 SDK 只负责底图；无定位请求，也不使用外部路线导航页面。 */
'use strict';
let points = [], current = null, map = null, markers = null;
let scale = 1, panX = 0, panY = 0, drag = null;
const canvas = document.getElementById('fallback');
const notice = document.getElementById('notice');
const context = canvas.getContext('2d');
function projected(point) {
    const latitude = Math.max(-85, Math.min(85, point[0])) * Math.PI / 180;
    return [point[1] * Math.PI / 180, Math.log(Math.tan(Math.PI / 4 + latitude / 2))];
}
function draw() {
    if (!points.length) return;
    const w = canvas.clientWidth, h = canvas.clientHeight;
    const dpr = window.devicePixelRatio || 1;
    canvas.width = w * dpr; canvas.height = h * dpr;
    context.setTransform(dpr, 0, 0, dpr, 0, 0);
    context.clearRect(0, 0, w, h);
    const path = points.map(projected);
    const xs = path.map(p => p[0]), ys = path.map(p => p[1]);
    const minX = Math.min(...xs), maxX = Math.max(...xs), minY = Math.min(...ys), maxY = Math.max(...ys);
    const factor = Math.min(Math.max(1, w - 100) / Math.max(maxX - minX, 1e-9), Math.max(1, h - 100) / Math.max(maxY - minY, 1e-9)) * scale;
    const screen = p => [(p[0] - (minX + maxX) / 2) * factor + w / 2 + panX, h / 2 - (p[1] - (minY + maxY) / 2) * factor + panY];
    context.strokeStyle = '#2476dc'; context.lineWidth = 5;
    context.lineJoin = 'round'; context.beginPath();
    path.forEach((p, i) => {const xy = screen(p); i ? context.lineTo(...xy) : context.moveTo(...xy);});
    context.stroke();
    function dot(p, color, label, labelOffset = -10) {
        const xy = screen(projected(p));
        context.beginPath(); context.arc(...xy, 8, 0, 2 * Math.PI);
        context.fillStyle = color; context.fill();
        context.strokeStyle = 'white'; context.lineWidth = 2; context.stroke();
        context.fillStyle = '#1f2937'; context.font = '14px sans-serif'; context.fillText(label, xy[0] + 12, xy[1] + labelOffset);
    }
    dot(points[0], '#16a34a', '起点'); dot(points[points.length - 1], '#dc2626', '充电站');
    if (current) dot(current, '#7c3aed', '模拟位置', 22);
}
function fit() {
    scale = 1; panX = panY = 0;
    if (map) {
        const bounds = new TMap.LatLngBounds();
        points.forEach(p => bounds.extend(new TMap.LatLng(p[0], p[1])));
        map.fitBounds(bounds, {padding: 60});
    } else draw();
}
function pin(color) {
    return 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" width="28" height="28"><circle cx="14" cy="14" r="11" fill="' + color + '" stroke="white" stroke-width="3"/></svg>');
}
function mapGeometry() {
    return [
        {id:'start', styleId:'start', position:new TMap.LatLng(...points[0])},
        {id:'end', styleId:'end', position:new TMap.LatLng(...points[points.length - 1])},
        {id:'position', styleId:'position', position:new TMap.LatLng(...current)}
    ];
}
function showFallback(message) {
    // SDK 即使在构造中抛错，也可能已创建覆盖层；必须让出鼠标事件。
    if (map) {
        try { map.destroy(); } catch (_) { /* 初始化不完整时仍继续清理 DOM。 */ }
    }
    map = null; markers = null;
    const container = document.getElementById('map');
    container.style.display = 'none';
    container.replaceChildren();
    canvas.style.display = 'block';
    notice.textContent = message;
    draw();
}
function initializeMap() {
    try {
        document.getElementById('map').style.display = 'block';
        map = new TMap.Map('map', {center: new TMap.LatLng(...points[0]), zoom:14, pitch:0});
        new TMap.MultiPolyline({map, styles:{route:new TMap.PolylineStyle({color:'#2476dc',width:7,borderWidth:2,borderColor:'#fff'})}, geometries:[{id:'route',styleId:'route',paths:points.map(p=>new TMap.LatLng(...p))}]});
        const styles = {};
        [['start','#16a34a'],['end','#dc2626'],['position','#7c3aed']].forEach(([id,color]) => {
            styles[id] = new TMap.MarkerStyle({width:28,height:28,anchor:{x:14,y:14},src:pin(color)});
        });
        markers = new TMap.MultiMarker({map, styles, geometries:mapGeometry()});
        fit(); canvas.style.display = 'none';
        notice.textContent = '绿色：起点 · 红色：充电站 · 紫色：模拟位置';
    } catch (_) {
        showFallback('底图不可用，显示路线示意图（可缩放、拖动）');
    }
}
window.showRoute = function(config) {
    points = config.route.polyline; current = points[0];
    draw();
    notice.textContent = '正在加载底图 · 当前为路线示意图';
    if (!config.key) {
        showFallback('底图未配置，显示路线示意图（可缩放、拖动）');
        return;
    }
    const script = document.createElement('script');
    script.src = 'https://map.qq.com/api/gljs?v=1.exp&key=' + encodeURIComponent(config.key);
    script.onload = initializeMap;
    script.onerror = () => showFallback('底图加载失败，显示路线示意图（可缩放、拖动）');
    document.head.appendChild(script);
    setTimeout(() => {if (!map) showFallback('底图暂不可用，显示路线示意图（可缩放、拖动）');}, 15000);
};
window.setPosition = function(latitude, longitude) {
    current = [latitude, longitude];
    if (markers) markers.setGeometries(mapGeometry());
    else draw();
};
canvas.addEventListener('wheel', event => {
    event.preventDefault(); scale = Math.max(.2, Math.min(20, scale * (event.deltaY < 0 ? 1.15 : 1 / 1.15))); draw();
}, {passive:false});
canvas.addEventListener('pointerdown', event => {drag = [event.clientX, event.clientY]; canvas.setPointerCapture(event.pointerId);});
canvas.addEventListener('pointermove', event => {
    if (!drag) return;
    panX += event.clientX - drag[0]; panY += event.clientY - drag[1]; drag = [event.clientX, event.clientY]; draw();
});
canvas.addEventListener('pointerup', () => {drag = null;});
canvas.addEventListener('pointercancel', () => {drag = null;});
window.addEventListener('resize', draw);
document.getElementById('fit').addEventListener('click', fit);
