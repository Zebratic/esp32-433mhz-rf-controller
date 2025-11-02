// API Documentation expandable sections
function toggleApiEndpoint(endpointId) {
    const endpoint = document.querySelector(`#${endpointId}`).closest('.api-endpoint');
    endpoint.classList.toggle('expanded');
    
    // Update toggle arrow
    const toggle = endpoint.querySelector('.api-toggle');
    if (endpoint.classList.contains('expanded')) {
        toggle.textContent = '▲';
    } else {
        toggle.textContent = '▼';
    }
}

