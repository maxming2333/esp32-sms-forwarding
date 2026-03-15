import { createApp } from 'vue'
import App from './App.vue'
import router from './router/index.js'
import './styles/main.scss'

// Inject ESP32 build-time data as a global (replaced by vite define)
// eslint-disable-next-line no-undef
window.__ESP32_DATA__ = typeof __ESP32_DATA__ !== 'undefined' ? __ESP32_DATA__ : {}

const app = createApp(App)
app.use(router)
app.mount('#app')

