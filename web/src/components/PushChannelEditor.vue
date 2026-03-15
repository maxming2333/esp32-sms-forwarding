<template>
  <div class="push-channel-card" :class="{ enabled: local.enabled }">
    <div class="push-channel-header" @click="local.enabled = !local.enabled">
      <input type="checkbox" :checked="local.enabled"
             @click.stop="local.enabled = !local.enabled" />
      <span class="channel-num">推送通道 {{ index + 1 }}</span>
      <span v-if="local.name" style="color:#888;font-size:13px">— {{ local.name }}</span>
    </div>

    <div v-if="local.enabled" class="push-channel-body">
      <!-- Channel name -->
      <div class="form-group">
        <label>通道名称</label>
        <input type="text" v-model="local.name" placeholder="自定义名称（便于识别）">
      </div>

      <!-- Push type selector -->
      <div class="form-group">
        <label>推送方式</label>
        <select v-model.number="local.type">
          <option v-for="pt in pushTypes" :key="pt.value" :value="pt.value">
            {{ pt.label }}
          </option>
        </select>
        <div v-if="typeInfo" class="push-type-hint">{{ typeInfo.hint }}</div>
      </div>

      <!-- URL (conditional label) -->
      <div v-if="typeInfo?.showUrl" class="form-group">
        <label>{{ typeInfo?.urlLabel || '推送URL/Webhook' }}</label>
        <input type="text" v-model="local.url"
               :placeholder="typeInfo?.urlPlaceholder || 'https://...'">
      </div>

      <!-- Key1 -->
      <div v-if="typeInfo?.showKey1" class="form-group">
        <label>{{ typeInfo?.key1Label || '参数1' }}</label>
        <input type="text" v-model="local.key1"
               :placeholder="typeInfo?.key1Placeholder || ''">
      </div>

      <!-- Key2 -->
      <div v-if="typeInfo?.showKey2" class="form-group">
        <label>{{ typeInfo?.key2Label || '参数2' }}</label>
        <input type="text" v-model="local.key2"
               :placeholder="typeInfo?.key2Placeholder || ''">
      </div>

      <!-- Custom body -->
      <div v-if="typeInfo?.showCustomBody" class="form-group">
        <label>请求体模板（使用 {sender} {message} {timestamp} {device} 占位符）</label>
        <textarea v-model="local.customBody" rows="4"
                  style="font-family:monospace;font-size:13px"
                  placeholder='{"key":"{sender}","value":"{message}"}'></textarea>
      </div>

      <!-- Test button -->
      <div style="display:flex;align-items:center;gap:12px;margin-top:8px">
        <button class="btn btn-orange btn-sm" :disabled="testing" @click="testChannel">
          {{ testing ? '⏳ 测试中...' : '🧪 测试推送' }}
        </button>
      </div>
      <div v-if="testResult" class="alert"
           :class="testResult.ok ? 'alert-success' : 'alert-error'"
           style="margin-top:8px">
        {{ testResult.ok ? '✅' : '❌' }} {{ testResult.message }}
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, watch } from 'vue'
import { useApi } from '../composables/useApi.js'

const props = defineProps({
  modelValue: { type: Object, required: true },
  index:      { type: Number, required: true }
})
const emit = defineEmits(['update:modelValue'])

const esp32 = window.__ESP32_DATA__ || {}
const pushTypes = esp32.PUSH_TYPES || []

// Local copy so edits are reactive
const local = ref({ ...props.modelValue })

// When local changes, emit to parent
watch(local, v => emit('update:modelValue', { ...v }), { deep: true })

// When parent changes, sync to local — but only when values actually differ
// to avoid an infinite watch cycle: local → emit → props → local → emit → ...
watch(() => props.modelValue, v => {
  if (JSON.stringify(v) !== JSON.stringify(local.value)) {
    local.value = { ...v }
  }
}, { deep: true })

const typeInfo = computed(() => pushTypes.find(pt => pt.value === local.value.type))

// Test
const api     = useApi()
const testing    = ref(false)
const testResult = ref(null)

async function testChannel() {
  testing.value    = true
  testResult.value = null
  try {
    const data = {
      type: local.value.type,
      url:  local.value.url  || '',
      key1: local.value.key1 || '',
      key2: local.value.key2 || '',
      body: local.value.customBody || ''
    }
    const res = await api.testPush(data)
    testResult.value = { ok: res?.success, message: res?.message || '未知结果' }
  } catch (e) {
    testResult.value = { ok: false, message: '请求失败: ' + e.message }
  } finally {
    testing.value = false
  }
}
</script>

