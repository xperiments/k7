import { createRoot } from 'react-dom/client';
import { Site } from './Site';
import '../index.css';

const root = document.getElementById('root');
if (!root) throw new Error('Missing #root');

createRoot(root).render(<Site />);
