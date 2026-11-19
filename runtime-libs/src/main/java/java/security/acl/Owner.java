package java.security.acl;

import java.security.Principal;

@Deprecated(since="9", forRemoval=true)
@SuppressWarnings("removal")
public interface Owner {

    public boolean addOwner(Principal caller, Principal owner)
      throws NotOwnerException;

    public boolean deleteOwner(Principal caller, Principal owner)
      throws NotOwnerException, LastOwnerException;

    public boolean isOwner(Principal owner);

}
