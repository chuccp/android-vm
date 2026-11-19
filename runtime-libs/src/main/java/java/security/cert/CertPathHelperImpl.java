package java.security.cert;

import java.util.Date;

import sun.security.provider.certpath.CertPathHelper;

class CertPathHelperImpl extends CertPathHelper {

    private CertPathHelperImpl() {

    }

    static synchronized void initialize() {
        if (CertPathHelper.instance == null) {
            CertPathHelper.instance = new CertPathHelperImpl();
        }
    }

    protected void implSetDateAndTime(X509CRLSelector sel, Date date, long skew) {
        sel.setDateAndTime(date, skew);
    }

    protected boolean implIsJdkCA(TrustAnchor anchor) {
        return anchor.isJdkCA();
    }
}
