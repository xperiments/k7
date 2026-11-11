import { createRoot } from 'react-dom/client';
import { registerServiceWorker } from '../pwa';
import { Site } from './Site';
import '../index.css';

const root = document.getElementById('root');
if (!root) throw new Error('Missing #root');

createRoot(root).render(<Site />);

/* The Site registers the worker so an install offered on `/` has one even when the visitor
 * has never opened `/app` (k7-site.md S4). It answers navigation for `/app` only, so the
 * Site itself always comes from the network. */
void registerServiceWorker();
