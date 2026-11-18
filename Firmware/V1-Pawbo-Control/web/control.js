"use strict";

const TOPIC = "pawbo/robot01";
const BROKER = "wss://d6d9f116.ala.asia-southeast1.emqxsl.com:8084/mqtt";
const HEARTBEAT_MS = 4500;
const ui = Object.fromEntries(
  [...document.querySelectorAll("[id]")].map((element) => [element.id, element])
);
const pending = new Map();
let client;
let robot;
let lastStatusAt = 0;
let alarmDirty = false;
let eyesDirty = false;

function feedback(text, error = false) {
  ui.feedback.textContent = text;
  ui.feedback.classList.toggle("offline", error);
}

function available() {
  return Boolean(client?.connected && robot?.online && robot.session &&
    lastStatusAt && performance.now() - lastStatusAt < HEARTBEAT_MS);
}

function timeLabel({hour, minute}) {
  return [hour, minute].map((part) => String(part).padStart(2, "0")).join(":");
}

function paint() {
  const online = available();
  const state = online ? robot.robot_state : null;
  const motion = online && !robot.fault && state !== "ALARM_ACTIVE";
  ui.connect.disabled = Boolean(client);
  ui.disconnect.disabled = !client;
  ui.online.textContent = online ? "ออนไลน์" : client?.connected ? "รอข้อมูลสดจากหุ่น / ออฟไลน์" : "รอข้อมูล";
  ui.online.classList.toggle("online", online);
  ui.online.classList.toggle("offline", !online && Boolean(client?.connected));
  ui.state.textContent = state ? [state, robot.fault].filter(Boolean).join(" · ") : "—";
  ui.servoState.textContent = online
    ? robot.fault ? "ตรวจสอบ PCA9685" :
      robot.active_servo ? String(robot.active_servo) + " / " + robot.target_angle + "°" : "PWM ปิด"
    : "—";
  ui.signal.textContent = online ? robot.wifi_rssi + " dBm / " + robot.uptime_s + " วินาที" : "—";
  ui.clock.textContent = online ? robot.clock_synced ? "ซิงก์แล้ว" : "รอซิงก์" : "—";
  ui.alarmState.textContent = online && robot.alarm
    ? timeLabel(robot.alarm) + (robot.alarm.ringing ? " · กำลังดัง" : robot.alarm.enabled ? " · เปิด" : " · ปิด")
    : "—";
  ui.eyeState.textContent = online ? ui.eyes.options[robot.eye_style]?.textContent ?? "—" : "—";
  if (online && robot.alarm && !alarmDirty) {
    ui.alarmTime.value = timeLabel(robot.alarm);
    ui.alarmEnabled.checked = robot.alarm.enabled;
  }
  if (online && !eyesDirty && ui.eyes.options[robot.eye_style]) {
    ui.eyes.value = String(robot.eye_style);
  }
  for (const element of [ui.servo, ui.angle, ui.move]) element.disabled = !motion;
  ui.stop.disabled = !online;
  for (const element of [ui.alarmTime, ui.alarmEnabled, ui.save]) element.disabled = !online;
  for (const element of [ui.eyes, ui.applyEyes]) element.disabled = !online || robot?.alarm?.ringing === true;
}

function clearPending() {
  if (pending.size) feedback("การเชื่อมต่อเปลี่ยนก่อนรับผลจากหุ่น", true);
  for (const command of pending.values()) clearTimeout(command.timer);
  pending.clear();
}

function receive(topic, data, packet) {
  let message;
  try {
    message = JSON.parse(data.toString());
  } catch {
    return;
  }
  if (!message || message.schema_version !== 1) return;
  if (topic === TOPIC + "/status" && message.device_id === "robot01") {
    if (message.online === false || (robot && message.session !== robot.session)) clearPending();
    robot = message;
    if (!packet.retain && message.online === true) lastStatusAt = performance.now();
    else if (message.online === false) lastStatusAt = 0;
    paint();
    return;
  }
  if (topic !== TOPIC + "/ack" || packet.retain) return;
  const command = pending.get(message.command_id);
  if (!command) return;
  if (message.status === "accepted") {
    feedback(command.label + " · หุ่นรับคำสั่งแล้ว");
    return;
  }
  if (!["completed", "rejected", "failed"].includes(message.status)) return;
  clearTimeout(command.timer);
  pending.delete(message.command_id);
  if (message.status === "completed") {
    if (command.route === "alarm" &&
        ui.alarmTime.value === timeLabel(command.fields) &&
        ui.alarmEnabled.checked === command.fields.enabled) alarmDirty = false;
    if (command.route === "eyes" && Number(ui.eyes.value) === command.fields.style) eyesDirty = false;
    feedback(command.label + " · หุ่นรายงานว่าสำเร็จ");
  } else {
    feedback(command.label + " · " + (message.error_code ?? message.status), true);
  }
}

ui.connection.addEventListener("submit", (event) => {
  event.preventDefault();
  if (client) return;
  if (!globalThis.mqtt) {
    ui.broker.textContent = "โหลด MQTT.js ไม่สำเร็จ ตรวจสอบอินเทอร์เน็ตแล้วเปิดหน้าใหม่";
    return;
  }
  const current = mqtt.connect(BROKER, {
    username: ui.username.value,
    password: ui.password.value,
    clean: true,
    protocolVersion: 4,
    connectTimeout: 7000,
    reconnectPeriod: 0,
    queueQoSZero: false
  });
  client = current;
  ui.broker.textContent = "กำลังเชื่อมต่อกับ Broker";
  paint();
  current.on("connect", () => {
    if (client !== current) return;
    ui.broker.textContent = "เชื่อมต่อกับ Broker แล้ว";
    current.subscribe([TOPIC + "/status", TOPIC + "/ack"], {qos: 1}, (error, grants) => {
      if (client !== current) return;
      if (error || grants?.length !== 2 || grants.some((grant) => grant.qos > 2)) {
        feedback("Broker ปฏิเสธการอ่าน status หรือ ACK ตรวจสิทธิ์บัญชีเว็บ", true);
        current.end(true);
      }
    });
    paint();
  });
  current.on("message", (topic, data, packet) => {
    if (client === current) receive(topic, data, packet);
  });
  current.on("error", (error) => {
    if (client === current) feedback("MQTT · " + error.message, true);
  });
  current.on("close", () => {
    if (client !== current) return;
    client = undefined;
    robot = undefined;
    lastStatusAt = 0;
    clearPending();
    ui.broker.textContent = "ตัดการเชื่อมต่อกับ Broker";
    paint();
  });
});
ui.disconnect.addEventListener("click", () => client?.end(true));

function send(route, action, fields, label) {
  if (!available()) {
    feedback("ยังไม่มีข้อมูลสดจากหุ่น ลองใหม่เมื่อหุ่นออนไลน์", true);
    return;
  }
  const id = [...crypto.getRandomValues(new Uint8Array(12))]
    .map((byte) => byte.toString(16).padStart(2, "0")).join("");
  const command = {
    schema_version: 1,
    command_id: id,
    action,
    session: robot.session,
    issued_at: new Date().toISOString().slice(0, 19) + "Z",
    ...fields
  };
  const timer = setTimeout(() => {
    pending.delete(id);
    feedback(label + " · หมดเวลารอผลจากหุ่น", true);
  }, 7000);
  pending.set(id, {timer, route, fields, label});
  feedback(label + " · รอหุ่นตอบกลับ");
  client.publish(TOPIC + "/cmd/" + route, JSON.stringify(command), {
    qos: route === "motion" ? 0 : 1,
    retain: false
  }, (error) => {
    if (error && pending.delete(id)) {
      clearTimeout(timer);
      feedback(label + " · " + error.message, true);
    }
  });
}

function moveServo() {
  if (ui.move.disabled) return;
  const servo = Number(ui.servo.value);
  const angle = Number(ui.angle.value);
  send("motion", "set_servo", {servo, angle, duration_ms: 650}, "Servo " + servo + " → " + angle + "°");
}
ui.angle.addEventListener("input", () => { ui.degrees.textContent = ui.angle.value + "°"; });
ui.angle.addEventListener("change", moveServo);
ui.move.addEventListener("click", moveServo);
ui.stop.addEventListener("click", () => send("stop", "stop", {}, "STOP"));
ui.alarm.addEventListener("input", () => { alarmDirty = true; });
ui.alarm.addEventListener("submit", (event) => {
  event.preventDefault();
  const [hour, minute] = ui.alarmTime.value.split(":").map(Number);
  if (!Number.isInteger(hour) || !Number.isInteger(minute)) return;
  send("alarm", "set_alarm", {hour, minute, enabled: ui.alarmEnabled.checked}, "บันทึกเวลาปลุก");
});
ui.eyes.addEventListener("change", () => { eyesDirty = true; });
ui.applyEyes.addEventListener("click", () =>
  send("eyes", "set_eyes", {style: Number(ui.eyes.value)}, "เปลี่ยนแบบตา")
);
setInterval(paint, 500);
paint();
