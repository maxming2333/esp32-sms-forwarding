<template>
  <div class="app-layout">
    <NavBar />
    <main class="page-container">
      <RouterView />
    </main>
    <footer class="app-footer">
      <a href="https://github.com/maxming2333/sms_forwarding" target="_blank" rel="noopener">
        maxming2333/sms_forwarding
      </a>
      &nbsp;·&nbsp; 低成本短信转发器
    </footer>
  </div>
</template>

<script setup>
import NavBar from './components/NavBar.vue'
</script>

