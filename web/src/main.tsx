import { createRoot } from 'react-dom/client';
import { registerServiceWorker } from './pwa';
import { App } from './ui/App';
import { startInstallWatch } from './ui/installStore';
import { startUpdateWatch } from './ui/updateStore';
import './index.css';

const root = document.getElementById('root');
if (!root) throw new Error('Missing #root');

// Before the first render: Chrome can fire `beforeinstallprompt` before a control that
// wants it exists, and the event can only be heard, not read back (A10).
startInstallWatch(window);

createRoot(root).render(<App />);

// A waiting worker is a banner, not a takeover (O6): the registration is kept here so the
// banner can ask it to skip waiting only when the user says so.
void registerServiceWorker().then((registration) => {
  if (registration !== null) startUpdateWatch(registration);
});
