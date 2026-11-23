const q = s => document.querySelector(s);
const text = s => q(s).textContent.trim();
const attr = (s,a) => q(s).getAttribute(a);
const hidden = s => {const r=q(s).getBoundingClientRect(); return r.width === 0 || r.height === 0 || getComputedStyle(q(s)).visibility === 'hidden';};
const visible = s => {if (!q(s) || hidden(s)) return false; const r=q(s).getBoundingClientRect(); return r.right>0 && r.left<800 && r.bottom>0 && r.top<600;};
