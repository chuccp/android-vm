package javax.net.ssl;

import java.security.AlgorithmConstraints;
import java.util.Map;
import java.util.List;
import java.util.HashMap;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.LinkedHashMap;

public class SSLParameters {

    private String[] cipherSuites;
    private String[] protocols;
    private boolean wantClientAuth;
    private boolean needClientAuth;
    private String identificationAlgorithm;
    private AlgorithmConstraints algorithmConstraints;
    private Map<Integer, SNIServerName> sniNames = null;
    private Map<Integer, SNIMatcher> sniMatchers = null;
    private boolean preferLocalCipherSuites;
    private boolean enableRetransmissions = true;
    private int maximumPacketSize = 0;
    private String[] applicationProtocols = new String[0];

    public SSLParameters() {

    }

    public SSLParameters(String[] cipherSuites) {
        setCipherSuites(cipherSuites);
    }

    public SSLParameters(String[] cipherSuites, String[] protocols) {
        setCipherSuites(cipherSuites);
        setProtocols(protocols);
    }

    private static String[] clone(String[] s) {
        return (s == null) ? null : s.clone();
    }

    public String[] getCipherSuites() {
        return clone(cipherSuites);
    }

    public void setCipherSuites(String[] cipherSuites) {
        this.cipherSuites = clone(cipherSuites);
    }

    public String[] getProtocols() {
        return clone(protocols);
    }

    public void setProtocols(String[] protocols) {
        this.protocols = clone(protocols);
    }

    public boolean getWantClientAuth() {
        return wantClientAuth;
    }

    public void setWantClientAuth(boolean wantClientAuth) {
        this.wantClientAuth = wantClientAuth;
        this.needClientAuth = false;
    }

    public boolean getNeedClientAuth() {
        return needClientAuth;
    }

    public void setNeedClientAuth(boolean needClientAuth) {
        this.wantClientAuth = false;
        this.needClientAuth = needClientAuth;
    }

    public AlgorithmConstraints getAlgorithmConstraints() {
        return algorithmConstraints;
    }

    public void setAlgorithmConstraints(AlgorithmConstraints constraints) {

        this.algorithmConstraints = constraints;
    }

    public String getEndpointIdentificationAlgorithm() {
        return identificationAlgorithm;
    }

    public void setEndpointIdentificationAlgorithm(String algorithm) {
        this.identificationAlgorithm = algorithm;
    }

    public final void setServerNames(List<SNIServerName> serverNames) {
        if (serverNames != null) {
            if (!serverNames.isEmpty()) {
                sniNames = new LinkedHashMap<>(serverNames.size());
                for (SNIServerName serverName : serverNames) {
                    if (sniNames.put(serverName.getType(),
                                                serverName) != null) {
                        throw new IllegalArgumentException(
                                    "Duplicated server name of type " +
                                    serverName.getType());
                    }
                }
            } else {
                sniNames = Collections.<Integer, SNIServerName>emptyMap();
            }
        } else {
            sniNames = null;
        }
    }

    public final List<SNIServerName> getServerNames() {
        if (sniNames != null) {
            if (!sniNames.isEmpty()) {
                return Collections.<SNIServerName>unmodifiableList(
                                        new ArrayList<>(sniNames.values()));
            } else {
                return Collections.<SNIServerName>emptyList();
            }
        }

        return null;
    }

    public final void setSNIMatchers(Collection<SNIMatcher> matchers) {
        if (matchers != null) {
            if (!matchers.isEmpty()) {
                sniMatchers = new HashMap<>(matchers.size());
                for (SNIMatcher matcher : matchers) {
                    if (sniMatchers.put(matcher.getType(),
                                                matcher) != null) {
                        throw new IllegalArgumentException(
                                    "Duplicated server name of type " +
                                    matcher.getType());
                    }
                }
            } else {
                sniMatchers = Collections.<Integer, SNIMatcher>emptyMap();
            }
        } else {
            sniMatchers = null;
        }
    }

    public final Collection<SNIMatcher> getSNIMatchers() {
        if (sniMatchers != null) {
            if (!sniMatchers.isEmpty()) {
                return Collections.<SNIMatcher>unmodifiableList(
                                        new ArrayList<>(sniMatchers.values()));
            } else {
                return Collections.<SNIMatcher>emptyList();
            }
        }

        return null;
    }

    public final void setUseCipherSuitesOrder(boolean honorOrder) {
        this.preferLocalCipherSuites = honorOrder;
    }

    public final boolean getUseCipherSuitesOrder() {
        return preferLocalCipherSuites;
    }

    public void setEnableRetransmissions(boolean enableRetransmissions) {
        this.enableRetransmissions = enableRetransmissions;
    }

    public boolean getEnableRetransmissions() {
        return enableRetransmissions;
    }

    public void setMaximumPacketSize(int maximumPacketSize) {
        if (maximumPacketSize < 0) {
            throw new IllegalArgumentException(
                "The maximum packet size cannot be negative");
        }

        this.maximumPacketSize = maximumPacketSize;
    }

    public int getMaximumPacketSize() {
        return maximumPacketSize;
    }

    public String[] getApplicationProtocols() {
        return applicationProtocols.clone();
    }

    public void setApplicationProtocols(String[] protocols) {
        if (protocols == null) {
            throw new IllegalArgumentException("protocols was null");
        }

        String[] tempProtocols = protocols.clone();

        for (String p : tempProtocols) {
            if (p == null || p.equals("")) {
                throw new IllegalArgumentException(
                    "An element of protocols was null/empty");
            }
        }
        applicationProtocols = tempProtocols;
    }
}
