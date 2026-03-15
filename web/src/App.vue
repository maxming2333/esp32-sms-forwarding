<template>
  <div class="app-layout">
    <NavBar />
    <main class="page-container">
      <RouterView />
    </main>
  </div>
</template>

<script setup>
import NavBar from './components/NavBar.vue'
</script>

