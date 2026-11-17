// 主题切换 + 摇杆交互（设计稿 Demo）
(function () {
    const KEY = "zp-design-theme";
    function apply(theme) {
        document.documentElement.setAttribute("data-theme", theme);
        if (document.body) document.body.setAttribute("data-theme", theme);
        const icon = document.getElementById("themeIcon");
        if (icon) {
            icon.innerHTML = theme === "light"
                ? '<path d="M21 12.8A9 9 0 1 1 11.2 3a7 7 0 0 0 9.8 9.8z"/>'
                : '<circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/>';
        }
        localStorage.setItem(KEY, theme);
    }
    window.toggleTheme = function () {
        const cur = document.documentElement.getAttribute("data-theme") || "light";
        apply(cur === "dark" ? "light" : "dark");
    };
    const saved = localStorage.getItem(KEY) || "light";
    document.addEventListener("DOMContentLoaded", () => apply(saved));

    // 摇杆拖拽演示：偏移方向与幅度体现「矢量控速」概念
    document.addEventListener("DOMContentLoaded", () => {
        const stick = document.querySelector(".joystick");
        if (!stick) return;
        const knob = stick.querySelector(".knob");
        let dragging = false;
        const R = 55; // 最大位移半径
        function move(e) {
            if (!dragging) return;
            const r = stick.getBoundingClientRect();
            const cx = r.left + r.width / 2, cy = r.top + r.height / 2;
            let dx = e.clientX - cx, dy = e.clientY - cy;
            const d = Math.hypot(dx, dy);
            if (d > R) { dx = dx / d * R; dy = dy / d * R; }
            knob.style.transform = `translate(${dx}px, ${dy}px)`;
            const panEl = document.querySelector("[data-pan]");
            const tiltEl = document.querySelector("[data-tilt]");
            const spdEl = document.querySelector("[data-spd]");
            if (panEl) panEl.textContent = (dx / R * 180 + 140).toFixed(1) + "°";
            if (tiltEl) tiltEl.textContent = (-(dy / R * 45) - 18).toFixed(1) + "°";
            if (spdEl) spdEl.textContent = Math.round(Math.min(d / R, 1) * 8) + "/8";
        }
        stick.addEventListener("pointerdown", e => {
            dragging = true; stick.setPointerCapture(e.pointerId); move(e);
        });
        stick.addEventListener("pointermove", move);
        stick.addEventListener("pointerup", () => {
            dragging = false; knob.style.transform = "translate(0,0)";
            const spdEl = document.querySelector("[data-spd]");
            if (spdEl) spdEl.textContent = "0/8";
        });
    });
})();
