import { mount } from 'svelte'
import './app.css'
import App from './App.svelte'
import { blockBrowserBehaviour } from './lib/guards'
import { forwardHostKeys, reportPointerDowns } from './lib/keys'

blockBrowserBehaviour()
forwardHostKeys()
reportPointerDowns()

const app = mount(App, {
  target: document.getElementById('app')!,
})

export default app
