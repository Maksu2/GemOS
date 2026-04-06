const filterRoot = document.querySelector("#status-filters");
const cards = Array.from(document.querySelectorAll(".status-card"));

if (filterRoot) {
  filterRoot.addEventListener("click", (event) => {
    const button = event.target.closest("[data-filter]");
    if (!button) return;

    const filter = button.dataset.filter;
    for (const item of filterRoot.querySelectorAll(".chip")) {
      item.classList.toggle("is-active", item === button);
    }

    for (const card of cards) {
      const tags = (card.dataset.tags || "").split(" ");
      const visible = filter === "all" || tags.includes(filter);
      card.classList.toggle("is-hidden", !visible);
    }
  });
}

const timelineButtons = Array.from(document.querySelectorAll(".timeline-item"));
const timelinePanels = Array.from(document.querySelectorAll(".timeline-panel"));

for (const button of timelineButtons) {
  button.addEventListener("click", () => {
    const target = button.dataset.target;
    for (const item of timelineButtons) {
      item.classList.toggle("is-open", item === button);
    }
    for (const panel of timelinePanels) {
      panel.classList.toggle("is-open", panel.id === target);
    }
  });
}

const copyButton = document.querySelector(".copy-button");

if (copyButton) {
  copyButton.addEventListener("click", async () => {
    const source = document.querySelector(`#${copyButton.dataset.copyTarget}`);
    if (!source) return;

    try {
      await navigator.clipboard.writeText(source.value);
      copyButton.textContent = "Copied";
      window.setTimeout(() => {
        copyButton.textContent = "Copy commands";
      }, 1200);
    } catch (_error) {
      copyButton.textContent = "Copy failed";
    }
  });
}

const revealItems = Array.from(document.querySelectorAll("[data-reveal]"));

if ("IntersectionObserver" in window) {
  const observer = new IntersectionObserver(
    (entries) => {
      for (const entry of entries) {
        if (!entry.isIntersecting) continue;
        entry.target.classList.add("is-visible");
        observer.unobserve(entry.target);
      }
    },
    { threshold: 0.16 }
  );

  for (const item of revealItems) {
    observer.observe(item);
  }
} else {
  for (const item of revealItems) {
    item.classList.add("is-visible");
  }
}
