document.addEventListener("DOMContentLoaded", () => {
  // Mobile navigation menu toggle
  const navToggle = document.getElementById("navToggle");
  const navLinks = document.getElementById("navLinks");

  if (navToggle && navLinks) {
    navToggle.addEventListener("click", () => {
      navLinks.classList.toggle("nav-open");
      const isExpanded = navLinks.classList.contains("nav-open");
      navToggle.setAttribute("aria-expanded", isExpanded);
    });

    // Close menu when clicking a link
    document.querySelectorAll(".nav-link").forEach((link) => {
      link.addEventListener("click", () => {
        navLinks.classList.remove("nav-open");
      });
    });

    // Close menu when clicking outside
    document.addEventListener("click", (e) => {
      if (!navToggle.contains(e.target) && !navLinks.contains(e.target)) {
        navLinks.classList.remove("nav-open");
      }
    });
  }

  // IntersectionObserver for reveal animations
  const reveals = document.querySelectorAll(".reveal");
  if ("IntersectionObserver" in window) {
    const observer = new IntersectionObserver(
      (entries, obs) => {
        entries.forEach((entry) => {
          if (entry.isIntersecting) {
            entry.target.classList.add("active");
            obs.unobserve(entry.target);
          }
        });
      },
      {
        threshold: 0.15,
        rootMargin: "0px 0px -40px 0px",
      }
    );

    reveals.forEach((el) => observer.observe(el));
  } else {
    // Fallback if IntersectionObserver isn't supported
    reveals.forEach((el) => el.classList.add("active"));
  }

  // Copy email button with toast feedback
  window.copyEmail = function (emailText = "goku33mm@gmail.com") {
    if (navigator.clipboard && window.isSecureContext) {
      navigator.clipboard
        .writeText(emailText)
        .then(() => showToast("Email copied to clipboard!"))
        .catch(() => fallbackCopy(emailText));
    } else {
      fallbackCopy(emailText);
    }
  };

  function fallbackCopy(text) {
    const textArea = document.createElement("textarea");
    textArea.value = text;
    textArea.style.position = "fixed";
    textArea.style.left = "-999999px";
    textArea.style.top = "-999999px";
    document.body.appendChild(textArea);
    textArea.focus();
    textArea.select();
    try {
      document.execCommand("copy");
      showToast("Email copied to clipboard!");
    } catch (err) {
      showToast("Email: " + text);
    }
    document.body.removeChild(textArea);
  }

  function showToast(message) {
    let toast = document.getElementById("toastNotice");
    if (!toast) {
      toast = document.createElement("div");
      toast.id = "toastNotice";
      toast.className = "toast-notice";
      document.body.appendChild(toast);
    }
    toast.textContent = message;
    toast.classList.add("show");

    setTimeout(() => {
      toast.classList.remove("show");
    }, 2800);
  }
});
