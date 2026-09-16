#include "RoboHTML/RoboHTMLTypes.h"

int robo_html_node_kind_is_known(robo_html_node_kind_t kind)
{
    switch (kind) {
    case ROBO_HTML_NODE_KIND_DOCUMENT:
    case ROBO_HTML_NODE_KIND_ELEMENT:
    case ROBO_HTML_NODE_KIND_TEXT:
    case ROBO_HTML_NODE_KIND_COMMENT:
    case ROBO_HTML_NODE_KIND_DOCUMENT_TYPE:
    case ROBO_HTML_NODE_KIND_FRAGMENT:
        return 1;
    default:
        return 0;
    }
}

const char *robo_html_node_kind_name(robo_html_node_kind_t kind)
{
    switch (kind) {
    case ROBO_HTML_NODE_KIND_DOCUMENT:
        return "document";
    case ROBO_HTML_NODE_KIND_ELEMENT:
        return "element";
    case ROBO_HTML_NODE_KIND_TEXT:
        return "text";
    case ROBO_HTML_NODE_KIND_COMMENT:
        return "comment";
    case ROBO_HTML_NODE_KIND_DOCUMENT_TYPE:
        return "doctype";
    case ROBO_HTML_NODE_KIND_FRAGMENT:
        return "fragment";
    default:
        return "unknown";
    }
}

int robo_html_namespace_is_known(robo_html_namespace_t ns)
{
    switch (ns) {
    case ROBO_HTML_NAMESPACE_NONE:
    case ROBO_HTML_NAMESPACE_HTML:
    case ROBO_HTML_NAMESPACE_SVG:
    case ROBO_HTML_NAMESPACE_MATHML:
        return 1;
    default:
        return 0;
    }
}

const char *robo_html_namespace_name(robo_html_namespace_t ns)
{
    switch (ns) {
    case ROBO_HTML_NAMESPACE_NONE:
        return "none";
    case ROBO_HTML_NAMESPACE_HTML:
        return "html";
    case ROBO_HTML_NAMESPACE_SVG:
        return "svg";
    case ROBO_HTML_NAMESPACE_MATHML:
        return "mathml";
    default:
        return "unknown";
    }
}

const char *robo_html_namespace_url(robo_html_namespace_t ns)
{
    switch (ns) {
    case ROBO_HTML_NAMESPACE_HTML:
        return "http://www.w3.org/1999/xhtml";
    case ROBO_HTML_NAMESPACE_SVG:
        return "http://www.w3.org/2000/svg";
    case ROBO_HTML_NAMESPACE_MATHML:
        return "http://www.w3.org/1998/Math/MathML";
    case ROBO_HTML_NAMESPACE_NONE:
    default:
        return "";
    }
}

int robo_html_diagnostic_domain_is_known(robo_html_diagnostic_domain_t domain)
{
    switch (domain) {
    case ROBO_HTML_DOMAIN_NONE:
    case ROBO_HTML_DOMAIN_DOCUMENT:
    case ROBO_HTML_DOMAIN_LIFECYCLE:
    case ROBO_HTML_DOMAIN_NODE:
    case ROBO_HTML_DOMAIN_SOURCE:
    case ROBO_HTML_DOMAIN_ENCODING:
    case ROBO_HTML_DOMAIN_PARSER:
    case ROBO_HTML_DOMAIN_RESOURCE:
    case ROBO_HTML_DOMAIN_CLIENT:
    case ROBO_HTML_DOMAIN_MUTATION:
    case ROBO_HTML_DOMAIN_RENDER:
    case ROBO_HTML_DOMAIN_TREE:
    case ROBO_HTML_DOMAIN_ELEMENT:
    case ROBO_HTML_DOMAIN_TEXT:
    case ROBO_HTML_DOMAIN_TRAVERSAL:
        return 1;
    default:
        return 0;
    }
}

const char *robo_html_diagnostic_domain_name(robo_html_diagnostic_domain_t domain)
{
    switch (domain) {
    case ROBO_HTML_DOMAIN_NONE:
        return "none";
    case ROBO_HTML_DOMAIN_DOCUMENT:
        return "document";
    case ROBO_HTML_DOMAIN_LIFECYCLE:
        return "lifecycle";
    case ROBO_HTML_DOMAIN_NODE:
        return "node";
    case ROBO_HTML_DOMAIN_SOURCE:
        return "source";
    case ROBO_HTML_DOMAIN_ENCODING:
        return "encoding";
    case ROBO_HTML_DOMAIN_PARSER:
        return "parser";
    case ROBO_HTML_DOMAIN_RESOURCE:
        return "resource";
    case ROBO_HTML_DOMAIN_CLIENT:
        return "client";
    case ROBO_HTML_DOMAIN_MUTATION:
        return "mutation";
    case ROBO_HTML_DOMAIN_RENDER:
        return "render";
    case ROBO_HTML_DOMAIN_TREE:
        return "tree";
    case ROBO_HTML_DOMAIN_ELEMENT:
        return "element";
    case ROBO_HTML_DOMAIN_TEXT:
        return "text";
    case ROBO_HTML_DOMAIN_TRAVERSAL:
        return "traversal";
    default:
        return "unknown";
    }
}
