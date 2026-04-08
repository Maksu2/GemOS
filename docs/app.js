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

const shotPicker = document.querySelector("#shot-picker");
const shotImage = document.querySelector("#showcase-image");
const shotKicker = document.querySelector("#showcase-kicker");
const shotTitle = document.querySelector("#showcase-title");
const shotDescription = document.querySelector("#showcase-description");

if (shotPicker && shotImage && shotKicker && shotTitle && shotDescription) {
  shotPicker.addEventListener("click", (event) => {
    const button = event.target.closest(".shot-thumb");
    if (!button) return;

    for (const item of shotPicker.querySelectorAll(".shot-thumb")) {
      item.classList.toggle("is-active", item === button);
    }

    shotImage.src = button.dataset.image;
    shotImage.alt = button.dataset.alt || "";
    shotKicker.textContent = button.dataset.kicker || "";
    shotTitle.textContent = button.dataset.title || "";
    shotDescription.textContent = button.dataset.description || "";
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
    { threshold: 0.12 }
  );

  for (const item of revealItems) {
    observer.observe(item);
  }
} else {
  for (const item of revealItems) {
    item.classList.add("is-visible");
  }
}
