#include "RoboHTML/RoboHTMLTypes.h"

int HtmlNodeKindIsKnown(HtmlNodeKind kind)
{
    switch (kind) {
    case RoboHtmlNodeKindDocument:
    case RoboHtmlNodeKindElement:
    case RoboHtmlNodeKindText:
    case RoboHtmlNodeKindComment:
    case RoboHtmlNodeKindDocumentType:
    case RoboHtmlNodeKindFragment:
        return 1;
    default:
        return 0;
    }
}

const char *HtmlNodeKindName(HtmlNodeKind kind)
{
    switch (kind) {
    case RoboHtmlNodeKindDocument:
        return "document";
    case RoboHtmlNodeKindElement:
        return "element";
    case RoboHtmlNodeKindText:
        return "text";
    case RoboHtmlNodeKindComment:
        return "comment";
    case RoboHtmlNodeKindDocumentType:
        return "doctype";
    case RoboHtmlNodeKindFragment:
        return "fragment";
    default:
        return "unknown";
    }
}

int HtmlNamespaceIsKnown(HtmlNamespace ns)
{
    switch (ns) {
    case RoboHtmlNamespaceNone:
    case RoboHtmlNamespaceHtml:
    case RoboHtmlNamespaceSvg:
    case RoboHtmlNamespaceMathml:
        return 1;
    default:
        return 0;
    }
}

const char *HtmlNamespaceName(HtmlNamespace ns)
{
    switch (ns) {
    case RoboHtmlNamespaceNone:
        return "none";
    case RoboHtmlNamespaceHtml:
        return "html";
    case RoboHtmlNamespaceSvg:
        return "svg";
    case RoboHtmlNamespaceMathml:
        return "mathml";
    default:
        return "unknown";
    }
}

const char *HtmlNamespaceUrl(HtmlNamespace ns)
{
    switch (ns) {
    case RoboHtmlNamespaceHtml:
        return "http://www.w3.org/1999/xhtml";
    case RoboHtmlNamespaceSvg:
        return "http://www.w3.org/2000/svg";
    case RoboHtmlNamespaceMathml:
        return "http://www.w3.org/1998/Math/MathML";
    case RoboHtmlNamespaceNone:
    default:
        return "";
    }
}

int HtmlDiagnosticDomainIsKnown(HtmlDiagnosticDomain domain)
{
    switch (domain) {
    case RoboHtmlDomainNone:
    case RoboHtmlDomainDocument:
    case RoboHtmlDomainLifecycle:
    case RoboHtmlDomainNode:
    case RoboHtmlDomainSource:
    case RoboHtmlDomainEncoding:
    case RoboHtmlDomainParser:
    case RoboHtmlDomainResource:
    case RoboHtmlDomainClient:
    case RoboHtmlDomainMutation:
    case RoboHtmlDomainRender:
    case RoboHtmlDomainTree:
    case RoboHtmlDomainElement:
    case RoboHtmlDomainText:
    case RoboHtmlDomainTraversal:
    case RoboHtmlDomainImage:
    case RoboHtmlDomainAnchor:
    case RoboHtmlDomainForms:
        return 1;
    default:
        return 0;
    }
}

const char *HtmlDiagnosticDomainName(HtmlDiagnosticDomain domain)
{
    switch (domain) {
    case RoboHtmlDomainNone:
        return "none";
    case RoboHtmlDomainDocument:
        return "document";
    case RoboHtmlDomainLifecycle:
        return "lifecycle";
    case RoboHtmlDomainNode:
        return "node";
    case RoboHtmlDomainSource:
        return "source";
    case RoboHtmlDomainEncoding:
        return "encoding";
    case RoboHtmlDomainParser:
        return "parser";
    case RoboHtmlDomainResource:
        return "resource";
    case RoboHtmlDomainClient:
        return "client";
    case RoboHtmlDomainMutation:
        return "mutation";
    case RoboHtmlDomainRender:
        return "render";
    case RoboHtmlDomainTree:
        return "tree";
    case RoboHtmlDomainElement:
        return "element";
    case RoboHtmlDomainText:
        return "text";
    case RoboHtmlDomainTraversal:
        return "traversal";
    case RoboHtmlDomainImage:
        return "image";
    case RoboHtmlDomainAnchor:
        return "anchor";
    case RoboHtmlDomainForms:
        return "forms";
    default:
        return "unknown";
    }
}
