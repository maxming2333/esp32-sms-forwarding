import { createRouter, createWebHashHistory } from 'vue-router'
import Config  from '../views/Config.vue'
import Tools   from '../views/Tools.vue'
import ApiDocs from '../views/ApiDocs.vue'

const routes = [
  { path: '/',         component: Config,  name: 'config'  },
  { path: '/tools',    component: Tools,   name: 'tools'   },
  { path: '/api-docs', component: ApiDocs, name: 'apidocs' },
]

export default createRouter({
  history: createWebHashHistory(),
  routes
})

