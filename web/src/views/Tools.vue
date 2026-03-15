<template>
  <div>
    <!-- Send SMS -->
    <div class="card">
      <div class="card-title">📤 发送短信</div>
      <div class="form-group">
        <label>目标号码</label>
        <input type="text" v-model="smsForm.phone"
               placeholder='填写手机号，国际号码用 "+国家码" 前缀，如 +8612345678900'>
      </div>
      <div class="form-group">
        <label>短信内容（已输入 {{ smsForm.content.length }} 字符）</label>
        <textarea v-model="smsForm.content" rows="4" placeholder="请输入短信内容..."></textarea>
      </div>
      <button class="btn btn-blue btn-block" :disabled="smsSending" @click="sendSms">
        {{ smsSending ? '⏳ 发送中...' : '📨 发送短信' }}
      </button>
      <div v-if="smsResult" class="alert" :class="smsResult.ok ? 'alert-success' : 'alert-error'"
           style="margin-top:10px">
        {{ smsResult.ok ? '✅' : '❌' }} {{ smsResult.message }}
      </div>
    </div>

    <!-- Module info query -->
    <div class="card">
      <div class="card-title">📊 模组信息查询</div>
      <div style="display:flex;gap:8px;flex-wrap:wrap">
        <button class="btn btn-ghost btn-sm" @click="query('ati')">📋 固件信息</button>
        <button class="btn btn-ghost btn-sm" @click="query('signal')">📶 信号质量</button>
        <button class="btn btn-ghost btn-sm" @click="query('siminfo')">💳 SIM卡信息</button>
        <button class="btn btn-ghost btn-sm" @click="query('network')">🌍 网络状态</button>
        <button class="btn btn-ghost btn-sm" @click="query('wifi')">📡 WiFi状态</button>
      </div>
      <div v-if="queryResult" class="alert" :class="queryResult.ok ? 'alert-info' : 'alert-error'"
           style="margin-top:12px" v-html="queryResult.message"></div>
    </div>

    <!-- Network test -->
    <div class="card">
      <div class="card-title">🌐 网络测试（Ping）</div>
      <p style="font-size:13px;color:#888;margin:0 0 12px">
        将向 8.8.8.8 发起 Ping，一次性消耗极少流量。
      </p>
      <button class="btn btn-orange btn-block" :disabled="pinging" @click="confirmPing">
        {{ pinging ? '⏳ Ping 中（最多 35 秒）...' : '📡 开始 Ping 测试' }}
      </button>
      <div v-if="pingResult" class="alert" :class="pingResult.ok ? 'alert-success' : 'alert-error'"
           style="margin-top:10px">
        {{ pingResult.ok ? '✅' : '❌' }} {{ pingResult.message }}
      </div>
    </div>

    <!-- Flight mode -->
    <div class="card">
      <div class="card-title">✈️ 模组控制（飞行模式）</div>
      <p style="font-size:13px;color:#888;margin:0 0 12px">
        飞行模式开启后模组将关闭射频，无法收发短信。
      </p>
      <div style="display:flex;gap:8px">
        <button class="btn btn-blue btn-sm" @click="flightQuery">🔍 查询状态</button>
        <button class="btn btn-red  btn-sm" @click="flightToggle">✈️ 切换飞行模式</button>
      </div>
      <div v-if="flightResult" class="alert" :class="flightResult.ok ? 'alert-info' : 'alert-error'"
           style="margin-top:10px" v-html="flightResult.message"></div>
    </div>

    <!-- AT debugger -->
    <div class="card">
      <div class="card-title">💻 AT 指令调试</div>
      <div class="at-log" ref="atLogEl">
        <div v-for="(entry, i) in atLog" :key="i">
          <span :class="entry.type">{{ entry.prefix }}</span>{{ entry.text }}
        </div>
        <span v-if="!atLog.length" style="color:#555">等待输入指令...</span>
      </div>
      <div style="display:flex;gap:8px">
        <input type="text" v-model="atCmd" placeholder="输入 AT 指令，如: AT+CSQ"
               style="flex:1;font-family:monospace"
               @keydown.enter="sendAT">
        <button class="btn btn-blue btn-sm" :disabled="atBusy" @click="sendAT">
          {{ atBusy ? '...' : '发送' }}
        </button>
        <button class="btn btn-ghost btn-sm" @click="atLog = []">🧹 清空</button>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, nextTick } from 'vue'
import { useApi } from '../composables/useApi.js'

const api = useApi()

// ── Send SMS ──────────────────────────────────────────────────────────────────
const smsForm    = ref({ phone: '', content: '' })
const smsSending = ref(false)
const smsResult  = ref(null)

async function sendSms() {
  if (!smsForm.value.phone || !smsForm.value.content) return
  smsSending.value = true; smsResult.value = null
  try {
    const r = await api.sendSms({ phone: smsForm.value.phone, content: smsForm.value.content })
    smsResult.value = { ok: r?.success, message: r?.message }
  } catch (e) { smsResult.value = { ok: false, message: e.message } }
  finally { smsSending.value = false }
}

// ── Query ─────────────────────────────────────────────────────────────────────
const queryResult = ref(null)
async function query(type) {
  queryResult.value = null
  try {
    const r = await api.query(type)
    queryResult.value = { ok: r?.success, message: r?.message }
  } catch (e) { queryResult.value = { ok: false, message: e.message } }
}

// ── Ping ──────────────────────────────────────────────────────────────────────
const pinging    = ref(false)
const pingResult = ref(null)

async function confirmPing() {
  if (!confirm('确定要执行 Ping 操作吗？\n这将消耗少量流量。')) return
  pinging.value = true; pingResult.value = null
  try {
    const r = await api.ping()
    pingResult.value = { ok: r?.success, message: r?.message }
  } catch (e) { pingResult.value = { ok: false, message: e.message } }
  finally { pinging.value = false }
}

// ── Flight mode ───────────────────────────────────────────────────────────────
const flightResult = ref(null)
async function flightQuery() {
  flightResult.value = null
  try {
    const r = await api.flight('query')
    flightResult.value = { ok: r?.success, message: r?.message }
  } catch (e) { flightResult.value = { ok: false, message: e.message } }
}
async function flightToggle() {
  if (!confirm('确定要切换飞行模式吗？\n开启后模组无法收发短信。')) return
  flightResult.value = null
  try {
    const r = await api.flight('toggle')
    flightResult.value = { ok: r?.success, message: r?.message }
  } catch (e) { flightResult.value = { ok: false, message: e.message } }
}

// ── AT debugger ───────────────────────────────────────────────────────────────
const atCmd   = ref('')
const atBusy  = ref(false)
const atLog   = ref([])
const atLogEl = ref(null)

async function sendAT() {
  const cmd = atCmd.value.trim()
  if (!cmd) return
  atLog.value.push({ type: 'at-cmd',  prefix: '> ', text: cmd })
  atCmd.value = ''; atBusy.value = true
  await nextTick()
  if (atLogEl.value) atLogEl.value.scrollTop = atLogEl.value.scrollHeight
  try {
    const r = await api.atCommand(cmd)
    atLog.value.push({ type: r?.success ? 'at-resp' : 'at-err',
                       prefix: '[RESP] ', text: r?.message || '无响应' })
  } catch (e) {
    atLog.value.push({ type: 'at-err', prefix: '❌ ', text: e.message })
  } finally {
    atBusy.value = false
    await nextTick()
    if (atLogEl.value) atLogEl.value.scrollTop = atLogEl.value.scrollHeight
  }
}
</script>

