import { createRoot } from 'react-dom/client';
import { registerServiceWorker } from './pwa';
import { App } from './ui/App';
import './styles.css';

const root = document.getElementById('root');
if (!root) throw new Error('Missing #root');

createRoot(root).render(<App />);
registerServiceWorker();
