// useApi.js — fetch wrapper that adds Basic Auth from sessionStorage
// The browser handles auth automatically on the first 401 challenge.
// This composable simply provides convenience methods for JSON APIs.

const BASE = ''  // same-origin; device serves on port 80

function getHeaders(extra = {}) {
  return { 'Content-Type': 'application/json', ...extra }
}

async function request(method, path, body = null) {
  const opts = { method, headers: getHeaders() }
  if (body) opts.body = typeof body === 'string' ? body : JSON.stringify(body)
  const res = await fetch(BASE + path, opts)
  if (res.status === 401) {
    // Force browser to prompt for credentials by re-fetching without credentials
    window.location.reload()
    return null
  }
  const ct = res.headers.get('content-type') || ''
  return ct.includes('application/json') ? res.json() : res.text()
}

async function formPost(path, formData) {
  const body = new URLSearchParams(formData).toString()
  const res = await fetch(BASE + path, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body
  })
  if (res.status === 401) { window.location.reload(); return null }
  const ct = res.headers.get('content-type') || ''
  return ct.includes('application/json') ? res.json() : res.text()
}

export function useApi() {
  return {
    getStatus:  ()         => request('GET',  '/api/status'),
    getConfig:  ()         => request('GET',  '/api/config'),
    saveConfig: (data)     => formPost('/api/config', data),
    sendSms:    (data)     => formPost('/api/sendsms', data),
    query:      (type)     => request('GET',  `/api/query?type=${type}`),
    flight:     (action)   => request('GET',  `/api/flight?action=${action}`),
    atCommand:  (cmd)      => request('GET',  `/api/at?cmd=${encodeURIComponent(cmd)}`),
    ping:       ()         => request('POST', '/api/ping'),
    testPush:   (data)     => formPost('/api/test_push', data),
  }
}

